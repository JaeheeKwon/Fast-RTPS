/*************************************************************************
 * Copyright (c) 2014 eProsima. All rights reserved.
 *
 * This copy of eProsima RTPS is licensed to you under the terms described in the
 * EPROSIMARTPS_LIBRARY_LICENSE file included in this distribution.
 *
 *************************************************************************/

/**
 * @file UnsentChangesNotEmptyEvent.cpp
 *
 */

#include "eprosimartps/rtps/writer/timedevent/UnsentChangesNotEmptyEvent.h"
#include "eprosimartps/rtps/writer/RTPSWriter.h"

#include "eprosimartps/rtps/participant/RTPSParticipantImpl.h"

#include "eprosimartps/utils/RTPSLog.h"

namespace eprosima {
namespace rtps {

static const char* const CLASS_NAME = "UnsentChangesNotEmptyEvent";

UnsentChangesNotEmptyEvent::UnsentChangesNotEmptyEvent(RTPSWriter* writer,
		double interval):
		TimedEvent(writer->getRTPSParticipant()->getIOService(),interval),
  mp_writer(writer)
{
	// TODO Auto-generated constructor stub

}

UnsentChangesNotEmptyEvent::~UnsentChangesNotEmptyEvent()
{

}

void UnsentChangesNotEmptyEvent::event(EventCode code, const char* msg)
{
	const char* const METHOD_NAME = "event";
	logInfo(RTPS_WRITER,"");
	if(code == EVENT_SUCCESS)
	{
		mp_writer->unsent_changes_not_empty();
	}
	else if(code == EVENT_ABORT)
	{
		logInfo(RTPS_WRITER,"UnsentChangesNotEmpty aborted");
		this->stopSemaphorePost();
	}
	else
	{
		logInfo(RTPS_WRITER,"UnsentChangesNotEmpty boost message: " <<msg);
	}
	delete(this);
}


} /* namespace rtps */
} /* namespace eprosima */
