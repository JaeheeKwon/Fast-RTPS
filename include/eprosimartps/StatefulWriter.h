/*************************************************************************
 * Copyright (c) 2014 eProsima. All rights reserved.
 *
 * This copy of eProsima RTPS is licensed to you under the terms described in the
 * EPROSIMARTPS_LIBRARY_LICENSE file included in this distribution.
 *
 *************************************************************************/

/**
 * @file StatefulWriter.h
 *
 *  Created on: Mar 17, 2014
 *      Author: Gonzalo Rodriguez Canosa
 *      email:  gonzalorodriguez@eprosima.com
 *              grcanosa@gmail.com  	
 */


#include "eprosimartps/rtps_all.h"
#include "eprosimartps/ReaderProxy.h"
#include "eprosimartps/RTPSWriter.h"



#ifndef STATEFULWRITER_H_
#define STATEFULWRITER_H_

namespace eprosima {
namespace rtps {

class StatefulWriter: public RTPSWriter {
public:
	StatefulWriter();
	virtual ~StatefulWriter();

	/**
	 * Initialize Stateful Writer.
	 * @param param Structure containing parameters.
	 */
	void init(WriterParams_t param);

	/**
	 * Add a matched reader to the writer.
	 * @param Rp Structure containing the parameters for the reader.
	 * @return True if correct.
	 */
	bool matched_reader_add(ReaderProxy_t Rp);
	/**
	 * Remove a reader from the writer list.
	 * @param Rp Structure containing the parameters.
	 * @return True if correct
	 */
	bool matched_reader_remove(ReaderProxy_t Rp);
	/**
	 * Remove a reder based on its guid.
	 * @param readerGuid GUID_t of the reader.
	 * @return True if correct.
	 */
	bool matched_reader_remove(GUID_t readerGuid);

	/**
	 * Find a Reader Proxy in this writer.
	 * @param[in] readerGuid The GUID_t of the reader.
	 * @param[out] RP Pointer to pointer to return the ReaderProxy.
	 * @return True if correct.
	 */
	bool matched_reader_lookup(GUID_t readerGuid,ReaderProxy** RP);

	/**
	 * Find out if a change is acked by all ReaderProxy.
	 * @param change Pointer to the change.
	 * @return True if correct.
	 */
	bool is_acked_by_all(CacheChange_t* change);


	void unsent_change_add(CacheChange_t* change);

	void unsent_changes_not_empty();


	std::vector<ReaderProxy*> matched_readers;

};

} /* namespace rtps */
} /* namespace eprosima */

#endif /* STATEFULWRITER_H_ */
