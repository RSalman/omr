/*******************************************************************************
 * Copyright (c) 1991, 2020 IBM Corp. and others
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


/**
 * @file
 * @ingroup GC_Modron_Standard
 */

#if !defined(SATBRootsTASK_HPP_)
#define SATBRootsTASK_HPP_

#include "omrcfg.h"

#include "CycleState.hpp"
#include "ConcurrentGCSATB.hpp"
#include "ParallelTask.hpp"
#include "ParallelTask.hpp"

class MM_EnvironmentBase;
class MM_MarkingScheme;
class MM_ParallelDispatcher;

/**
 * @todo Provide class documentation
 * @ingroup GC_Modron_Standard
 */
class MM_SATBRootsTask : public MM_ParallelTask
{
private:
	MM_MarkingScheme *_markingScheme;
	MM_CycleState *_cycleState;  /**< Collection cycle state active for the task */
	MM_ConcurrentGCSATB *_collector;
	
public:
	virtual uintptr_t getVMStateID() { return OMRVMSTATE_GC_MARK; };
	
	virtual void run(MM_EnvironmentBase *env);
	virtual void setup(MM_EnvironmentBase *env);
	virtual void cleanup(MM_EnvironmentBase *env);

	/**
	 * Create a SATBRootsTask object.
	 */
	MM_SATBRootsTask(MM_EnvironmentBase *env,
			MM_ParallelDispatcher *dispatcher,
			MM_ConcurrentGCSATB *collector,
			MM_MarkingScheme *markingScheme, 
			MM_CycleState *cycleState) :
		MM_ParallelTask(env, dispatcher)
		,_markingScheme(markingScheme)
		,_cycleState(cycleState)
		,_collector(collector)
	{
		_typeId = __FUNCTION__;
	};
};

#endif /* SATBRootsTASK_HPP_ */
