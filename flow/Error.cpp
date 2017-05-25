/*
 * Error.cpp
 *
 * This source file is part of the FoundationDB open source project
 *
 * Copyright 2013-2018 Apple Inc. and the FoundationDB project authors
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Error.h"
#include "Trace.h"
#include "Knobs.h"
#include <iostream>
using std::cout;
using std::endl;
using std::make_pair;

bool g_crashOnError = false;

std::map<int, int>& Error::errorCounts() {
	static std::map<int, int> counts;
	return counts;
}

#include <iostream>

extern void flushTraceFileVoid();

Error Error::fromUnvalidatedCode(int code) {
	if (code < 0 || code > 30000) {
		Error e = Error::fromCode(error_code_unknown_error);
		TraceEvent(SevWarn, "ConvertedUnvalidatedErrorCode").detail("OriginalCode", code).error(e);
		return e;
	}
	else
		return Error::fromCode(code);
}

Error internal_error_impl( const char* file, int line ) {
	fprintf(stderr, "Internal Error @ %s %d:\n  %s\n", file, line, platform::get_backtrace().c_str());

	TraceEvent(SevError, "InternalError")
		.error(Error::fromCode(error_code_internal_error))
		.detail("File", file)
		.detail("Line", line)
		.backtrace();
	flushTraceFileVoid();
	return Error(error_code_internal_error);
}

Error::Error(int error_code)
	: error_code(error_code), flags(0)
{
	if (TRACE_SAMPLE()) TraceEvent(SevSample, "ErrorCreated").detail("ErrorCode", error_code);
	//std::cout << "Error: " << error_code << std::endl;
	if (error_code >= 3000 && error_code < 6000) {
		TraceEvent(SevError, "SystemError").error(*this).backtrace();
		if (g_crashOnError) {
			flushOutputStreams();
			flushTraceFileVoid();
			crashAndDie();
		}
	}
	/*if (error_code)
		errorCounts()[error_code]++;*/
}

ErrorCodeTable& Error::errorCodeTable() {
	static ErrorCodeTable table;
	return table;
}

const char* Error::what() const {
	auto table = errorCodeTable();
	auto it = table.find(error_code);
	if (it == table.end()) return "UNKNOWN_ERROR";
	return it->second;
}

void Error::init() {
	errorCodeTable();
}

Error Error::asInjectedFault() const {
	Error e = *this;
	e.flags |= FLAG_INJECTED_FAULT;
	return e;
}

ErrorCodeTable::ErrorCodeTable() {
	#define ERROR(name, number, comment) (*this)[number] = #name; enum { Duplicate_Error_Code_##number = 0 };
	#include "error_definitions.h"
}

void ErrorCodeTable::addCode(int code, const char* message) {
	(*this)[code] = message;
}

bool isAssertDisabled(int line) {
	return FLOW_KNOBS->DISABLE_ASSERTS == -1 || FLOW_KNOBS->DISABLE_ASSERTS == line;
}