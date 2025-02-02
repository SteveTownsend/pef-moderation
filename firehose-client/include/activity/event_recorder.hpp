#ifndef __event_recorder_hpp__
#define __event_recorder_hpp__
/*************************************************************************
Public Education Forum Moderation Firehose Client
Copyright (c) Steve Townsend 2024

>>> SOURCE LICENSE >>>
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation (www.fsf.org); either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

A copy of the GNU General Public License is available at
http://www.fsf.org/licensing/licenses
>>> END OF LICENSE >>>
*************************************************************************/

#include "activity/event_cache.hpp"
#include "readerwriterqueue.h"

namespace activity {
class event_recorder {
public:
  event_recorder();
  void wait_enqueue(timed_event &&value);

private:
  // Declare queue between post-processing and recording
  moodycamel::BlockingReaderWriterQueue<timed_event> _queue;
  std::thread _thread;

  event_cache _events;
};
} // namespace activity

#endif