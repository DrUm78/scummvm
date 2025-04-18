/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef ULTIMA8_KERNEL_KERNEL_H
#define ULTIMA8_KERNEL_KERNEL_H

#include "ultima/shared/std/containers.h"
#include "ultima/ultima8/usecode/intrinsics.h"

namespace Ultima {
namespace Ultima8 {

class Debugger;
class Process;
class idMan;

typedef Process *(*ProcessLoadFunc)(Common::ReadStream *rs, uint32 version);
typedef Std::list<Process *>::const_iterator ProcessIter;
typedef Std::list<Process *>::iterator ProcessIterator;


class Kernel {
	friend class Debugger;
public:
	Kernel();
	~Kernel();

	static Kernel *get_instance() {
		return _kernel;
	}

	void reset();

	// returns pid of new process
	ProcId addProcess(Process *proc, bool dispose = true);

	//! add a process and run it immediately
	//! \return pid of process
	ProcId addProcessExec(Process *proc, bool dispose = true);

	void runProcesses();
	Process *getProcess(ProcId pid);

	ProcId assignPID(Process *proc);

	void setNextProcess(Process *proc);
	Process *getRunningProcess() const {
		return _runningProcess;
	}

	// objid = 0 means any object, type = 6 means any type
	uint32 getNumProcesses(ObjId objid, uint16 processtype);

	//! find a (any) process of the given objid, processtype
	Process *findProcess(ObjId objid, uint16 processtype);

	//! kill (fail) processes of a certain object and/or of a certain type
	//! \param objid the object, or 0 for any object (except objid 0)
	//! \param type the type, or 6 for any type
	//! \param fail if true, fail the processes instead of terminating them
	void killProcesses(ObjId objid, uint16 processtype, bool fail);

	//! kill (fail) processes of a certain object and not of a certain type
	//! \param objid the object, or 0 for any object (except objid 0)
	//! \param type the type not to kill
	//! \param fail if true, fail the processes instead of terminating them
	void killProcessesNotOfType(ObjId objid, uint16 processtype, bool fail);

	//! kill (fail) processes not of a certain type, regardless of object ID
	//! except for the current running process (for switching levels in Crusader)
	//! \param type the type not to kill
	//! \param fail if true, fail the processes instead of terminating them
	void killAllProcessesNotOfTypeExcludeCurrent(uint16 processtype, bool fail);

	//! get an iterator of the process list.
	ProcessIter getProcessBeginIterator() {
		return _processes.begin();
	}
	ProcessIter getProcessEndIterator() {
		return _processes.end();
	}

	void kernelStats();
	void processTypes();

	bool canSave();
	void save(Common::WriteStream *ws);
	bool load(Common::ReadStream *rs, uint32 version);

	void pause() {
		_paused++;
	}
	void unpause() {
		if (_paused > 0)
			_paused--;
	}
	bool isPaused() const {
		return _paused > 0;
	}

	void setFrameByFrame(bool fbf) {
		_frameByFrame = fbf;
	}
	bool isFrameByFrame() const {
		return _frameByFrame;
	}

	void addProcessLoader(Std::string classname, ProcessLoadFunc func) {
		_processLoaders[classname] = func;
	}

	uint32 getFrameNum() const {
		return _tickNum / TICKS_PER_FRAME;
	};
	uint32 getTickNum() const {
		return _tickNum;
	};

	static const uint32 TICKS_PER_FRAME;
	static const uint32 TICKS_PER_SECOND;
	static const uint32 FRAMES_PER_SECOND;

	// A special process type which means kill all the processes.
	static const uint16 PROC_TYPE_ALL;

	INTRINSIC(I_getNumProcesses);
	INTRINSIC(I_resetRef);
private:
	Process *loadProcess(Common::ReadStream *rs, uint32 version);

	Std::list<Process *> _processes;
	idMan   *_pIDs;

	Std::list<Process *>::iterator _currentProcess;

	Common::HashMap<Common::String, ProcessLoadFunc> _processLoaders;

	bool _loading;

	uint32 _tickNum;
	unsigned int _paused;
	bool _frameByFrame;

	Process *_runningProcess;

	static Kernel *_kernel;
};


extern const uint U8_RAND_MAX;
extern uint getRandom();


} // End of namespace Ultima8
} // End of namespace Ultima

#endif
