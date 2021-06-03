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


#include "omrcfg.h"
#include "omrmodroncore.h"
#include "omrport.h"
#include "modronopt.h"
#include "ut_j9mm.h"

//#include "ConcurrentMarkingSATBDelegate.hpp"

#include "SATBRootsTask.hpp"

#include "EnvironmentBase.hpp"
#include "MarkingScheme.hpp"
#include "WorkStack.hpp"


void
MM_SATBRootsTask::run(MM_EnvironmentBase *env)
{
	_markingScheme->markLiveObjectsInit(env, false);
	_collector->getSATBDelegate()->markLiveObjectsRoots(env);

	env->_workStack.flush(env);
}

void
MM_SATBRootsTask::setup(MM_EnvironmentBase *env)
{
	if(env->isMainThread()) {
		Assert_MM_true(_cycleState == env->_cycleState);
	} else {
		Assert_MM_true(NULL == env->_cycleState);
		env->_cycleState = _cycleState;
	}
}

void
MM_SATBRootsTask::cleanup(MM_EnvironmentBase *env)
{
	if (env->isMainThread()) {
		Assert_MM_true(_cycleState == env->_cycleState);
	} else {
		env->_cycleState = NULL;
	}
}
