// ParallelSortCS.hlsl
//
// Copyright(c) 2021 Advanced Micro Devices, Inc.All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.


//--------------------------------------------------------------------------------------
// ParallelSort Shaders/Includes
//--------------------------------------------------------------------------------------
#define FFX_HLSL
#include "FFX-ParallelSort/FFX_ParallelSort.h"

[[vk::binding(0, 0)]] ConstantBuffer<FFX_ParallelSortCB>	CBuffer		: register(b0);					// Constant buffer

struct RootConstantData {
	uint CShiftBit;
};

#ifdef VK_Const
	[[vk::push_constant]] RootConstantData rootConstData;												// Store the shift bit directly in the root signature
#else
	ConstantBuffer<RootConstantData> rootConstData	: register(b1);										// Store the shift bit directly in the root signature
#endif // VK_Const

#ifdef kRS_ValueCopy
[[vk::binding(0, 1)]] RWStructuredBuffer<uint>	SrcBuffer			: register(u0, space0);					// The unsorted keys/payloads
[[vk::binding(0, 1)]] RWStructuredBuffer<uint64_t>	SrcBuffer64		: register(u0, space0);					// The unsorted keys/payloads
#else
[[vk::binding(0, 1)]] RWStructuredBuffer<uint>	SrcBuffer		: register(u0, space0);					// The unsorted keys
#endif // kRS_ValueCopy

[[vk::binding(0, 2)]] RWStructuredBuffer<uint>	SumTable		: register(u0, space1);					// The sum table we will write sums to

// FPS Count
[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void FPS_Count(uint localID : SV_GroupThreadID, uint groupID : SV_GroupID)
{
	// Call the uint version of the count part of the algorithm
	FFX_ParallelSort_Count_uint(localID, groupID, CBuffer, rootConstData.CShiftBit, SrcBuffer, SumTable);
}
