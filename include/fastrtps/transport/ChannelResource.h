// Copyright 2018 Proyectos y Sistemas de Mantenimiento SL (eProsima).
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef CHANNEL_RESOURCE_INFO_
#define CHANNEL_RESOURCE_INFO_

#include <memory>
#include <map>
#include <fastrtps/utils/Semaphore.h>
#include <fastrtps/log/Log.h>
#include <fastrtps/rtps/common/CDRMessage_t.h>

namespace eprosima{
namespace fastrtps{
namespace rtps{

class ChannelResource
{
public:
    ChannelResource();
    ChannelResource(ChannelResource&& channelResource);
    ChannelResource(uint32_t rec_buffer_size);
    virtual ~ChannelResource();
    virtual void Clear();

    inline void SetThread(std::thread* pThread)
    {
        mThread = pThread;
    }

    std::thread* ReleaseThread();

    inline bool IsAlive() const
    {
        return mAlive;
    }

    inline virtual void Disable()
    {
        mAlive = false;
    }

    inline CDRMessage_t& GetMessageBuffer()
    {
        return m_rec_msg;
    }

protected:
    //!Received message
    CDRMessage_t m_rec_msg;

    std::atomic<bool> mAlive;
    std::thread* mThread;
};

} // namespace rtps
} // namespace fastrtps
} // namespace eprosima

#endif // CHANNEL_RESOURCE_INFO_