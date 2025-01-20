/*
   Copyright 2025 Joseph W. Seigh
   
   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#pragma once

//------------------------------------------------------------------------------
// atomix.h -- basic atomic primatives and memory barriers
//
// version -- 0.0
//
//
//
//
//
//------------------------------------------------------------------------------

#ifndef __ATOMIXX_H
#define __ATOMIXX_H

#include <atomic>

/**
 * @brief atomic compare and exchange
 * @tparam T type must have size and alignment of 16
 * @param var      128 bit location to be updated
 * @param expected 128 bit expected value
 * @param desired  128 bit update value
 * @returns true - var updated
 *          false - update failed, expected set to current value of var
 */
template<typename T>
inline bool atomic_compare_exchange_16xx(T& var, T& expected, T desired, std::memory_order order)
{
	static_assert(sizeof(T) == 16);
	static_assert(alignof(T) == 16);

	bool rc;

	__asm__ __volatile__ (
		"lea	%3, %%rsi            ;\n"	// address of exchange
		"movq	0(%%rsi), %%rbx      ;\n"	// exchange low
		"movq	8(%%rsi), %%rcx      ;\n"	// exchange high

		"mov	%2, %%rsi            ;\n"	// comparand
		"movq	0(%%rsi), %%rax      ;\n"	// comparand low
		"movq	8(%%rsi), %%rdx      ;\n"	// comparand high
		
		"mov	%1, %%rsi            ;\n"	// destination
		"lock cmpxchg16b (%%rsi)     ;\n"
		"jz  	1f                   ;\n"
		"mov	%2, %%rsi            ;\n"	// comparand
		"movq	%%rax, 0(%%rsi)      ;\n"	// comparand low	
		"movq	%%rdx, 8(%%rsi)      ;\n"	// comparand high
"1:      setz	%b0                  ;\n"	// rc = 
		: "=&a" (rc)
		: "m" (&var), "m" (&expected), "m" (desired)
		: "cc", "memory", "rdx", "rbx", "rcx", "rsi"
		);

	return rc;
}



#endif // __ATOMIXX_H
/*-*/
