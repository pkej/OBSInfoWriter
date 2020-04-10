#include "InfoWriter.h"

#include <Groundfloor/Atoms/Defines.h>
#include <Groundfloor/Materials/Functions.h>
#include <cstdint>
#include <cmath>
#include <memory>
#include <regex>

#include "OutputFormat/OutputFormat.EDL.h"
#include "OutputFormat/OutputFormat.Default.h"

const char* c_TimestampNotation = "%Y-%m-%d %H:%M:%S";


InfoWriter::InfoWriter()
{
   StartTime = 0;
   StartRecordTime = 0;
   StartStreamTime = 0;
   PausedTotalTime = 0;
   PausedStartTime = 0;
   StreamStarted = false;
   RecordStarted = false;
   ShowStreaming = false;
   Paused = false;
}

std::string InfoWriter::SecsToHMSString(const int64_t totalseconds) const
{
	uint32_t hr = (uint32_t)trunc(totalseconds / 60.0 / 60.0);
	uint32_t min = (uint32_t)trunc(totalseconds / 60.0) - (hr * 60);
	uint32_t sec = totalseconds % 60;

	std::string format = Settings.GetFormat();
	std::string replacement = "\t";
	std::regex tabregex("(\\\\t)");
	format = std::regex_replace(format, tabregex, replacement);
	format += "\0\0\0\0";

	char buffer[1024];
	sprintf(&buffer[0], format.c_str(), hr, min, sec);

	return buffer;
}

int64_t InfoWriter::getPausedTime(const int64_t currentTime) const
{
	auto PausedTmpTime = PausedTotalTime;

	if (Paused)
	{
		PausedTmpTime = PausedTotalTime + (currentTime - PausedStartTime);
	}

	return PausedTmpTime;
}

void InfoWriter::WriteInfo(const std::string AExtraInfo) const
{
	if (!HasStarted()) return;

	std::string Info;
	auto currentTime = Groundfloor::GetTimestamp();
	auto tmpTime = SecsToHMSString(0);

	auto PausedTmpTime = getPausedTime(currentTime);

	if (RecordStarted)
	{
		tmpTime = SecsToHMSString((currentTime - StartRecordTime) - PausedTmpTime);
	}

	std::string record_info = tmpTime;
	tmpTime = SecsToHMSString(0);
	if (StreamStarted)
	{
		tmpTime = SecsToHMSString(currentTime - StartStreamTime);
	}
	auto stream_info = tmpTime;

	record_info += " Record Time Marker";
	stream_info += " Stream Time Marker";

	if (!RecordStarted) record_info += " (not recording)";
	if (Paused) record_info += " (recording is paused)";
	if (!StreamStarted) stream_info += " (not streaming)";

	record_info += "\n";
	stream_info += "\n";

	if (AExtraInfo.length() != 0)
	{
		Info = "";
		Info += AExtraInfo;
		Info += "\n";
	}

	Info += record_info;
	if (ShowStreaming) Info += stream_info;

	output->TextMarker(Info);
}

void InfoWriter::WriteInfo(const InfoHotkey AHotkey) const
{
    if (!HasStarted()) return;

    auto Now = Groundfloor::GetTimestamp();
    auto hotkey_text = Settings.GetHotkeyText(AHotkey);  

	 if (lastInfoMediaType == imtStream) {
		 output->HotkeyMarker(Now - StartStreamTime, hotkey_text);
	 }
	 else {
		 output->HotkeyMarker(Now - StartRecordTime - getPausedTime(Now), hotkey_text);
	 }
}

void InfoWriter::WriteSceneChange(const std::string scenename) const
{
	if (!HasStarted()) return;

	auto Now = Groundfloor::GetTimestamp();

	if (lastInfoMediaType == imtStream) {
		if (scenename == "") {
			output->ScenechangeMarker(Now - StartStreamTime, "UNKNO");
		}
		else {
			output->ScenechangeMarker(Now - StartStreamTime, scenename);
		}
	}
	else {
		if (scenename == "") {
			output->ScenechangeMarker(Now - StartRecordTime - getPausedTime(Now), "UNKNO");
		}
		else {
			output->ScenechangeMarker(Now - StartRecordTime - getPausedTime(Now), scenename);
		}
	}
}

void InfoWriter::InitCurrentFilename(int64_t timestamp)
{
  CurrentFilename = Settings.GetFilename();
  if (CurrentFilename.find('%') != 0) {
      auto filename = Groundfloor::TimestampToStr(CurrentFilename.c_str(), StartTime);
      CurrentFilename = filename->getValue();
      delete filename;
  }
}

void InfoWriter::MarkStart(InfoMediaType AType)
{
   StartTime = Groundfloor::GetTimestamp();
   InitCurrentFilename(StartTime);

	// todo: get format from settings
	output = std::make_unique<OutputFormatDefault>(Settings, CurrentFilename);

	output->Start();

   auto MarkStr = Groundfloor::TimestampToStr(c_TimestampNotation, StartTime);

	lastInfoMediaType = AType;

   switch (AType) {
   case imtStream:
	   StartStreamTime = StartTime;
	   StreamStarted = true;
	   if (ShowStreaming)
	   {
		   MarkStr->prepend_ansi("EVENT:START STREAM @ ");
	   }
		output->TextMarker(MarkStr->getValue());
      break;
   case imtUnknown:
      MarkStr->prepend_ansi(" (WARNING:Unsure how we STARTED) ");
   case imtRecording:
      MarkStr->prepend_ansi("EVENT:START RECORDING @ ");
	  StartRecordTime = StartTime;
	  RecordStarted = true;
	  Paused = false;
	  output->TextMarker(MarkStr->getValue());
      break;
   }

   delete MarkStr;
}

void InfoWriter::MarkStop(InfoMediaType AType)
{
	if (!HasStarted()) return;

	auto Now = Groundfloor::GetTimestamp();
	auto MarkStr = Groundfloor::TimestampToStr(c_TimestampNotation, Now);

	switch (AType) {
	case imtStream:
		output->Stop(Now - StartStreamTime);
		MarkStr->prepend_ansi("EVENT:STOP STREAM @ ");
		MarkStr->append(" Stream Time Marker Reset to 0");	
		StreamStarted = false;
		StartStreamTime = 0;
		break;
	case imtUnknown:
	case imtRecording:
		output->Stop(Now - StartRecordTime - getPausedTime(Now));
		MarkStr->prepend_ansi("EVENT:STOP RECORDING @ ");
		MarkStr->append(" Record Time Marker Reset to 0");
		StartRecordTime = 0;  //reset times 
		PausedTotalTime = 0;
		Paused = false;
		RecordStarted = false;
		break;
	}

	WriteInfo(MarkStr->getValue());

	delete MarkStr;
}

void InfoWriter::MarkPauseStart(const InfoMediaType AType)
{
	if (!HasStarted()) return;

	Paused = true;
   PausedStartTime = Groundfloor::GetTimestamp();

	output->PausedMarker(PausedStartTime - StartTime);
}    

void InfoWriter::MarkPauseResume(const InfoMediaType AType)
{
	if (!HasStarted()) return;

	Paused = false;
	auto CurrentTime = Groundfloor::GetTimestamp();
	PausedTotalTime += (CurrentTime - PausedStartTime);

	output->ResumedMarker(CurrentTime - StartTime, CurrentTime - PausedStartTime);
}

bool InfoWriter::HasStarted() const
{
	if (StreamStarted) return true;
	if (RecordStarted) return true;
	return false;
}

bool InfoWriter::IsStreaming() const
{
	return StreamStarted;
}

bool InfoWriter::ShowStreamOutput() const
{
	return ShowStreaming;
}

void InfoWriter::SetShowStreamOutput(bool logchanges)
{
	ShowStreaming = logchanges;
}

std::string InfoWriter::NowTimeStamp() const
{
	auto NowStr = Groundfloor::TimestampToStr(c_TimestampNotation, StartTime);
	return NowStr->getValue();
}

InfoWriterSettings *InfoWriter::GetSettings()
{
   return &Settings;
}
