/*******************************************************************************
 * Copyright (c) 2016, 2018 IBM Corp. and others
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at https://www.eclipse.org/legal/epl-2.0/
 * or the Apache License, Version 2.0 which accompanies this distribution and
 * is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following
 * Secondary Licenses when the conditions for such availability set
 * forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
 * General Public License, version 2 with the GNU Classpath
 * Exception [1] and GNU General Public License, version 2 with the
 * OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] http://openjdk.java.net/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
 *******************************************************************************/

#include "ModronAssertions.h"

#include "Dispatcher.hpp"
#include "EnvironmentBase.hpp"
#include "GCExtensionsBase.hpp"
#include "ScavengerStats.hpp"

#include "ScavengerCopyScanRatio.hpp"

#define DEBUG_HIS 0
void
MM_ScavengerCopyScanRatio::reset(MM_EnvironmentBase* env, bool resetHistory)
{
	_accumulatingSamples = 0;
	_accumulatedSamples = SCAVENGER_COUNTER_DEFAULT_ACCUMULATOR;
	_threadCount = env->getExtensions()->dispatcher->activeThreadCount();
	if (resetHistory) {
		OMRPORT_ACCESS_FROM_OMRPORT(env->getPortLibrary());
		_resetTimestamp = omrtime_hires_clock();
		_majorUpdateThreadEnv = 0;
		_scalingUpdateCount = 0;
		_overflowCount = 0;
		_flushCount = 0;
		_localFlushes = 0;
		_missed = 0;
		_missed_extra = 0;
		_historyFoldingFactor = 1;
		_historyTableIndex = 0;
		memset(_historyTable, 0, SCAVENGER_UPDATE_HISTORY_SIZE * sizeof(UpdateHistory));
	}
}

uintptr_t
MM_ScavengerCopyScanRatio::update(MM_EnvironmentBase* env, uint64_t *slotsScanned, uint64_t *slotsCopied, uint64_t waitingCount, uintptr_t nonEmptyScanLists, uintptr_t cachesQueued, bool flush)
{
	OMRPORT_ACCESS_FROM_OMRPORT(env->getPortLibrary());
	bool debug = false;
	if(!flush) {
		env->cachedWaitCount = waitingCount;
	}

	//omrtty_printf("UPDATE START [Slots Scanned %zu] [Slots Coppied %zu] \n", *slotsScanned, *slotsCopied);

	if (SCAVENGER_SLOTS_SCANNED_PER_THREAD_UPDATE <= *slotsScanned || flush) {
		//omrtty_printf("UPDATE START\n");
		uint64_t scannedCount =  *slotsScanned;
		uint64_t copiedCount =  *slotsCopied;
		*slotsScanned = *slotsCopied = 0;



		/* this thread may have scanned a long array segment resulting in scanned/copied slot counts that must be scaled down to avoid overflow in the accumulator */
		while ((SCAVENGER_SLOTS_SCANNED_PER_THREAD_UPDATE << 1) < scannedCount) {
			/* scale scanned and copied counts identically */
			scannedCount >>= 1;
			copiedCount >>= 1;
		}

//		if(flush) {
//			omrtty_printf("\tAccumulator: [waitingCount: %u] [updates: %u] \n", waits(_accumulatingSamples), updates(_accumulatingSamples));
//		}

		/* add this thread's samples to the accumulating register */
		uint64_t updateSample = sample(scannedCount, copiedCount, waitingCount);
		uint64_t updateResult = atomicAddThreadUpdate(env, updateSample, nonEmptyScanLists, cachesQueued, flush);
		uint64_t updateCount = updates(updateResult);

		env->_totalUpdates++;

		if(flush)
			MM_AtomicOperations::add(&_localFlushes , 1);

		//if(flush) {
		if(debug)	{if(updateResult == 0 &&flush )
				omrtty_printf("[%i] UPDATE [count: %u] [# in AS: %i] [FLUSH MISSED - %llx] \n", env->getSlaveID(), env->_totalUpdates, updateCount, updateSample);
			//else
			//	omrtty_printf("[%i] UPDATE [count: %u] [# in AS: %i] [FLUSH] \n", env->getSlaveID(), env->_totalUpdates, updateCount);
		//} else{
			else if(updateResult == 0)
				omrtty_printf("[%i] UPDATE [count: %u] [# in AS: %i] [MISSED - %llx] \n", env->getSlaveID(), env->_totalUpdates, updateCount, updateSample);
		//	else
		//		omrtty_printf("[%i] UPDATE [count: %u] [# in AS: %i] \n", env->getSlaveID(), env->_totalUpdates, updateCount);

		//}
		}


		/* this next section includes a critical region for the thread that increments the update counter to threshold */
		if (SCAVENGER_THREAD_UPDATES_PER_MAJOR_UPDATE == updateCount) {
			/* make sure that every other thread knows that a specific thread is performing the major update. if
			 * this thread gets timesliced in the section below while other free-running threads work up another major
			 * update, that update will be discarded */

			omrtty_printf("** [%i] ATTEMPT QUEUE Major Update: %llx **\n", env->getSlaveID(), updateResult);
			if  (0 == MM_AtomicOperations::lockCompareExchange(&_majorUpdateThreadEnv, 0, (uintptr_t)env)) {
				omrtty_printf("** [%i] QUEUE Major Update: %llx **\n", env->getSlaveID(), updateResult);
				return updateResult;
			} else {
				omrtty_printf("[%i] CANDIDATE MAX DISCARD \n", env->getSlaveID());
			}
		}
	}

	return 0;
}

void
MM_ScavengerCopyScanRatio::majorUpdate(MM_EnvironmentBase* env, uint64_t updateResult, uintptr_t nonEmptyScanLists, uintptr_t cachesQueued, bool flush) {
	OMRPORT_ACCESS_FROM_OMRPORT(env->getPortLibrary());

	Assert_MM_true(updates(updateResult) == SCAVENGER_THREAD_UPDATES_PER_MAJOR_UPDATE);

	if (0 == (SCAVENGER_COUNTER_OVERFLOW & updateResult)) {
		/* no overflow so latch updateResult into _accumulatedSamples and record the update */
		MM_AtomicOperations::setU64(&_accumulatedSamples, updateResult);

		MM_AtomicOperations::add(&_flushCount , _localFlushes);
		MM_AtomicOperations::setU64(&_localFlushes, 0);

		_scalingUpdateCount += 1;
			omrtty_printf("** [%i] Major Update DONE: %llx **\n",env->getSlaveID(), _accumulatedSamples);
		_threadCount = record(env, flush ? nonEmptyScanLists :nonEmptyScanListsFlushCache , flush ? cachesQueuedFlushCache : cachesQueued);
	} else {
		/* one or more counters overflowed so discard this update */
		_overflowCount += 1;
		omrtty_printf("** Major Update OVERFLOW: %zu \t  waits: [%zu] copied: [%zu] scanned:[%zu] updates[%zu]  ** \n", updateResult, waits(updateResult), copied(updateResult), scanned(updateResult), updates(updateResult) );
	}
	_majorUpdateThreadEnv = 0;
	MM_AtomicOperations::storeSync();
}

void
MM_ScavengerCopyScanRatio::flush(MM_EnvironmentBase* env) {

	OMRPORT_ACCESS_FROM_OMRPORT(env->getPortLibrary());



	uint64_t updateResult = 0;

		if (_accumulatingSamples == 0) {
			return ;
		} else {
			updateResult = _accumulatingSamples;
			Assert_MM_true(updates(updateResult) <= SCAVENGER_THREAD_UPDATES_PER_MAJOR_UPDATE);
			MM_AtomicOperations::setU64(&_accumulatingSamples, 0);
		//	omrtty_printf("_accumulatingSample is holding %u that need to be flushed\n", updates(updateResult));
			MM_AtomicOperations::add(&_flushCount , updates(updateResult));
			MM_AtomicOperations::setU64(&_localFlushes, 0);
		//	omrtty_printf("** Major Update [flush]: %llx **\n", updateResult);

		}

		if (updateResult != 0 && 0 == (SCAVENGER_COUNTER_OVERFLOW & updateResult)) {
			/* no overflow so latch _accumulatingSamples into _accumulatedSamples and record the update */
			MM_AtomicOperations::setU64(&_accumulatedSamples, updateResult);
			_scalingUpdateCount += 1;
			_threadCount = record(env, nonEmptyScanListsFlushCache, cachesQueuedFlushCache);
		} else {

			omrtty_printf("** Flush OVERFLOW: %zu ** \n", updateResult);

			/* one or more counters overflowed so discard this update */
			_overflowCount += 1;
		}
		Assert_MM_true(_majorUpdateThreadEnv == 0);
	}

uintptr_t
MM_ScavengerCopyScanRatio::record(MM_EnvironmentBase* env, uintptr_t nonEmptyScanLists, uintptr_t cachesQueued)
{
	OMRPORT_ACCESS_FROM_OMRPORT(env->getPortLibrary());

	if(DEBUG_HIS)omrtty_printf("RECORDING \n");


	if (SCAVENGER_UPDATE_HISTORY_SIZE <= _historyTableIndex) {
		Assert_MM_true(SCAVENGER_UPDATE_HISTORY_SIZE == _historyTableIndex);
		/* table full -- sum adjacent pairs of records and shift results to top half of table */
		UpdateHistory *head = &(_historyTable[0]);
		UpdateHistory *tail = &(_historyTable[1]);
		UpdateHistory *stop = &(_historyTable[SCAVENGER_UPDATE_HISTORY_SIZE]);
		while (tail < stop) {
			UpdateHistory *prev = tail - 1;
			prev->waits += tail->waits;
			prev->copied += tail->copied;
			prev->scanned += tail->scanned;
			prev->updates += tail->updates;
			prev->threads += tail->threads;
			prev->majorUpdates += tail->majorUpdates;
			prev->lists += tail->lists;
			prev->caches += tail->caches;
#if defined(OMR_GC_CONCURRENT_SCAVENGER)
			prev->readObjectBarrierUpdate = tail->readObjectBarrierUpdate;
			prev->readObjectBarrierCopy = tail->readObjectBarrierCopy;
#endif /* OMR_GC_CONCURRENT_SCAVENGER */
			prev->time = tail->time;
			if (prev > head) {
				memcpy(head, prev, sizeof(UpdateHistory));
			}
			head += 1;
			tail += 2;
		}
		_historyFoldingFactor <<= 1;
		_historyTableIndex = SCAVENGER_UPDATE_HISTORY_SIZE >> 1;
		uintptr_t zeroBytes = (SCAVENGER_UPDATE_HISTORY_SIZE >> 1) * sizeof(UpdateHistory);
		memset(&(_historyTable[_historyTableIndex]), 0, zeroBytes);
	}

	/* update record at current table index from fields in current acculumator */
	uintptr_t threadCount = env->getExtensions()->dispatcher->activeThreadCount();
	UpdateHistory *historyRecord = &(_historyTable[_historyTableIndex]);
	uint64_t accumulatedSamples = _accumulatedSamples;
	historyRecord->waits += waits(accumulatedSamples);
	historyRecord->copied += copied(accumulatedSamples);
	historyRecord->scanned += scanned(accumulatedSamples);
	historyRecord->updates += updates(accumulatedSamples);
	historyRecord->threads += threadCount;
	historyRecord->majorUpdates += 1;
	historyRecord->lists += nonEmptyScanLists;
	historyRecord->caches += cachesQueued;
#if defined(OMR_GC_CONCURRENT_SCAVENGER)
	/* record current read barries values (we do not want to sum them up and average, we want last value) */
	MM_GCExtensionsBase *ext = env->getExtensions();
	historyRecord->readObjectBarrierUpdate = ext->scavengerStats._readObjectBarrierUpdate;
	historyRecord->readObjectBarrierCopy = ext->scavengerStats._readObjectBarrierCopy;
#endif /* OMR_GC_CONCURRENT_SCAVENGER */
	historyRecord->time = omrtime_hires_clock();

	/* advance table index if current record is maxed out */
	if (historyRecord->updates >= (_historyFoldingFactor * SCAVENGER_THREAD_UPDATES_PER_MAJOR_UPDATE)) {
		_historyTableIndex += 1;
	}
	return threadCount;
}

uint64_t
MM_ScavengerCopyScanRatio::atomicAddThreadUpdate(MM_EnvironmentBase* env, uint64_t threadUpdate, uintptr_t nonEmptyScanLists, uintptr_t cachesQueued, bool flush)
{
	OMRPORT_ACCESS_FROM_OMRPORT(env->getPortLibrary());
	bool debug = false;
	uint64_t newValue = 0;
	/* Stop compiler optimizing away load of oldValue */
	volatile uint64_t *localAddr = &_accumulatingSamples;
	uint64_t oldValue = *localAddr;


	if (oldValue == MM_AtomicOperations::lockCompareExchangeU64(localAddr, oldValue, oldValue + threadUpdate)) {

		if(!flush) {
			nonEmptyScanListsFlushCache = nonEmptyScanLists;
			cachesQueuedFlushCache = cachesQueued;
		}

		// Is it possible that old value does not contain other updates before clearing the acculating sampels global?
		//Since old value is a snapshot of accumlatingSamples before this thread updates it's value.
		newValue = oldValue + threadUpdate;

		uint64_t updateCount = updates(newValue);
		if (SCAVENGER_THREAD_UPDATES_PER_MAJOR_UPDATE <= updateCount) {


			//By clearing AS are we loosing any updates?
			//Perhaps, any updates that are not part of newValue, or futhre not part of oldValue which will be used for the major update
			//because we loose AS and return newValue represneting AS

			//How do we know if there are updates not in newValue that are cleared in AS?
			//Ideally newValue - AS = 0?

			if(debug) omrtty_printf("[%i] [newValue: %u - %llx] threadUpdate:  %llx \n", env->getSlaveID(), updateCount, newValue, threadUpdate);

			uintptr_t asc;
			if ( (asc = updates(_accumulatingSamples)) != updateCount){
				if(debug)omrtty_printf("[%i] [AS: %u] [newValue: %u - %llx] \n", env->getSlaveID(), asc, updateCount, newValue);
			}


			if(debug)	omrtty_printf("[%i] ABOUT TO 0 AS \n", env->getSlaveID());
			MM_AtomicOperations::setU64(&_accumulatingSamples, 0);
			if(debug)omrtty_printf("[%i] COMPLETE 0 OF AS \n", env->getSlaveID());

			if (SCAVENGER_THREAD_UPDATES_PER_MAJOR_UPDATE < updateCount) {
				MM_AtomicOperations::add(&_missed_extra, 1);
				newValue = 0;
			}
		} else {
			if(debug)	omrtty_printf("[%i] [newValue: %u - %llx] threadUpdate:  %llx \n", env->getSlaveID(), updateCount, newValue, threadUpdate);
		}
	} else {
		MM_AtomicOperations::add(&_missed, 1);
	}

	return newValue;
}

uint64_t
MM_ScavengerCopyScanRatio::getSpannedMicros(MM_EnvironmentBase* env, UpdateHistory *historyRecord)
{
	OMRPORT_ACCESS_FROM_OMRPORT(env->getPortLibrary());
	uint64_t start = (historyRecord == _historyTable) ? _resetTimestamp : (historyRecord - 1)->time;
	return omrtime_hires_delta(start, historyRecord->time, OMRPORT_TIME_DELTA_IN_MICROSECONDS);
}

void
MM_ScavengerCopyScanRatio::failedUpdate(MM_EnvironmentBase* env, uint64_t copied, uint64_t scanned)
{
	Assert_GC_true_with_message2(env, copied <= scanned, "MM_ScavengerCopyScanRatio::getScalingFactor(): copied (=%llu) exceeds scanned (=%llu) -- non-atomic 64-bit read\n", copied, scanned);
}

