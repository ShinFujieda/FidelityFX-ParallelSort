// FFX_ParallelSort.h
//
// Copyright (c) 2020 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#define FFX_PARALLELSORT_SORT_BITS_PER_PASS		4
#define	FFX_PARALLELSORT_SORT_BIN_COUNT			(1 << FFX_PARALLELSORT_SORT_BITS_PER_PASS)
#define FFX_PARALLELSORT_SORT_BIN_PACKED		(FFX_PARALLELSORT_SORT_BIN_COUNT / 4)
#define FFX_PARALLELSORT_ELEMENTS_PER_THREAD	3
#define FFX_PARALLELSORT_THREADGROUP_SIZE		128

//////////////////////////////////////////////////////////////////////////
// ParallelSort constant buffer parameters:
//
//	NumKeys								The number of keys to sort
//	Shift								How many bits to shift for this sort pass (we sort 4 bits at a time)
//	NumBlocksPerThreadGroup				How many blocks of keys each thread group needs to process
//	NumThreadGroups						How many thread groups are being run concurrently for sort
//	NumThreadGroupsWithAdditionalBlocks	How many thread groups need to process additional block data
//	NumReduceThreadgroupPerBin			How many thread groups are summed together for each reduced bin entry
//	NumScanValues						How many values to perform scan prefix (+ add) on
//////////////////////////////////////////////////////////////////////////

#ifdef FFX_CPP
	struct FFX_ParallelSortCB
	{
		uint32_t NumKeys;
		int32_t  NumBlocksPerThreadGroup;
		uint32_t NumThreadGroups;
		uint32_t NumThreadGroupsWithAdditionalBlocks;
		uint32_t NumReduceThreadgroupPerBin;
		uint32_t NumScanValues;
	};

	void FFX_ParallelSort_CalculateScratchResourceSize(uint32_t MaxNumKeys, uint32_t& ScratchBufferSize, uint32_t& ReduceScratchBufferSize)
	{
		uint32_t BlockSize = FFX_PARALLELSORT_ELEMENTS_PER_THREAD * FFX_PARALLELSORT_THREADGROUP_SIZE;
		uint32_t NumBlocks = (MaxNumKeys + BlockSize - 1) / BlockSize;
		uint32_t NumReducedBlocks = (NumBlocks + BlockSize - 1) / BlockSize;

		ScratchBufferSize = FFX_PARALLELSORT_SORT_BIN_COUNT * NumBlocks * sizeof(uint32_t);
		ReduceScratchBufferSize = FFX_PARALLELSORT_SORT_BIN_COUNT * NumReducedBlocks * sizeof(uint32_t);
	}

	void FFX_ParallelSort_SetConstantAndDispatchData(uint32_t NumKeys, uint32_t MaxThreadGroups, FFX_ParallelSortCB& ConstantBuffer, uint32_t& NumThreadGroupsToRun, uint32_t& NumReducedThreadGroupsToRun)
	{
		ConstantBuffer.NumKeys = NumKeys;

		uint32_t BlockSize = FFX_PARALLELSORT_ELEMENTS_PER_THREAD * FFX_PARALLELSORT_THREADGROUP_SIZE;
		uint32_t NumBlocks = (NumKeys + BlockSize - 1) / BlockSize;

		// Figure out data distribution
		NumThreadGroupsToRun = MaxThreadGroups;
		uint32_t BlocksPerThreadGroup = (NumBlocks / NumThreadGroupsToRun);
		ConstantBuffer.NumThreadGroupsWithAdditionalBlocks = NumBlocks % NumThreadGroupsToRun;

		if (NumBlocks < NumThreadGroupsToRun)
		{
			BlocksPerThreadGroup = 1;
			NumThreadGroupsToRun = NumBlocks;
			ConstantBuffer.NumThreadGroupsWithAdditionalBlocks = 0;
		}

		ConstantBuffer.NumThreadGroups = NumThreadGroupsToRun;
		ConstantBuffer.NumBlocksPerThreadGroup = BlocksPerThreadGroup;

		// Calculate the number of thread groups to run for reduction (each thread group can process BlockSize number of entries)
		NumReducedThreadGroupsToRun = FFX_PARALLELSORT_SORT_BIN_COUNT * ((BlockSize > NumThreadGroupsToRun) ? 1 : (NumThreadGroupsToRun + BlockSize - 1) / BlockSize);
		ConstantBuffer.NumReduceThreadgroupPerBin = NumReducedThreadGroupsToRun / FFX_PARALLELSORT_SORT_BIN_COUNT;
		ConstantBuffer.NumScanValues = NumReducedThreadGroupsToRun;	// The number of reduce thread groups becomes our scan count (as each thread group writes out 1 value that needs scan prefix)
	}

	// We are using some optimizations to hide buffer load latency, so make sure anyone changing this define is made aware of that fact.
	static_assert(FFX_PARALLELSORT_ELEMENTS_PER_THREAD == 3, "FFX_ParallelSort Shaders currently explicitly rely on FFX_PARALLELSORT_ELEMENTS_PER_THREAD being set to 3 in order to optimize buffer loads. Please adjust the optimization to factor in the new define value.");
#elif defined(FFX_HLSL)

	struct FFX_ParallelSortCB
	{
		uint NumKeys;
		int  NumBlocksPerThreadGroup;
		uint NumThreadGroups;
		uint NumThreadGroupsWithAdditionalBlocks;
		uint NumReduceThreadgroupPerBin;
		uint NumScanValues;
	};

	groupshared uint gs_FFX_PARALLELSORT_Histogram[FFX_PARALLELSORT_THREADGROUP_SIZE * FFX_PARALLELSORT_SORT_BIN_COUNT];
	void FFX_ParallelSort_Count_uint(uint localID, uint groupID, FFX_ParallelSortCB CBuffer, uint ShiftBit,	RWStructuredBuffer<uint> SrcBuffer,	RWStructuredBuffer<uint> SumTable)
	{
		// Start by clearing our local counts in LDS
		for (int i = 0; i < FFX_PARALLELSORT_SORT_BIN_COUNT; i++)
			gs_FFX_PARALLELSORT_Histogram[(i * FFX_PARALLELSORT_THREADGROUP_SIZE) + localID] = 0;

		// Wait for everyone to catch up
		GroupMemoryBarrierWithGroupSync();

		// Data is processed in blocks, and how many we process can changed based on how much data we are processing
		// versus how many thread groups we are processing with
		int BlockSize = FFX_PARALLELSORT_ELEMENTS_PER_THREAD * FFX_PARALLELSORT_THREADGROUP_SIZE;

		// Figure out this thread group's index into the block data (taking into account thread groups that need to do extra reads)
		uint ThreadgroupBlockStart = (BlockSize * CBuffer.NumBlocksPerThreadGroup * groupID);
		uint NumBlocksToProcess = CBuffer.NumBlocksPerThreadGroup;

		if (groupID >= CBuffer.NumThreadGroups - CBuffer.NumThreadGroupsWithAdditionalBlocks)
		{
			ThreadgroupBlockStart += (groupID - (CBuffer.NumThreadGroups - CBuffer.NumThreadGroupsWithAdditionalBlocks)) * BlockSize;
			NumBlocksToProcess++;
		}

		// Get the block start index for this thread
		uint BlockIndex = ThreadgroupBlockStart + localID;

		// Count value occurrence
		for (uint BlockCount = 0; BlockCount < NumBlocksToProcess; BlockCount++, BlockIndex += BlockSize)
		{
			uint DataIndex = BlockIndex;

			// Pre-load the key values in order to hide some of the read latency
			uint srcKeys[FFX_PARALLELSORT_ELEMENTS_PER_THREAD];
#ifdef kRS_ValueCopy
			srcKeys[0] = SrcBuffer[2*DataIndex];
			srcKeys[1] = SrcBuffer[2*(DataIndex + FFX_PARALLELSORT_THREADGROUP_SIZE)];
			srcKeys[2] = SrcBuffer[2*(DataIndex + (FFX_PARALLELSORT_THREADGROUP_SIZE * 2))];
			// srcKeys[3] = SrcBuffer[2*(DataIndex + (FFX_PARALLELSORT_THREADGROUP_SIZE * 3))];
#else
			srcKeys[0] = SrcBuffer[DataIndex];
			srcKeys[1] = SrcBuffer[DataIndex + FFX_PARALLELSORT_THREADGROUP_SIZE];
			srcKeys[2] = SrcBuffer[DataIndex + (FFX_PARALLELSORT_THREADGROUP_SIZE * 2)];
			// srcKeys[3] = SrcBuffer[DataIndex + (FFX_PARALLELSORT_THREADGROUP_SIZE * 3)];
#endif // kRS_ValueCopy

			for (uint i = 0; i < FFX_PARALLELSORT_ELEMENTS_PER_THREAD; i++)
			{
				if (DataIndex < CBuffer.NumKeys)
				{
					uint localKey = (srcKeys[i] >> ShiftBit) & 0xf;
					InterlockedAdd(gs_FFX_PARALLELSORT_Histogram[(localKey * FFX_PARALLELSORT_THREADGROUP_SIZE) + localID], 1);
					DataIndex += FFX_PARALLELSORT_THREADGROUP_SIZE;
				}
			}
		}

		// Even though our LDS layout guarantees no collisions, our thread group size is greater than a wave
		// so we need to make sure all thread groups are done counting before we start tallying up the results
		GroupMemoryBarrierWithGroupSync();

		if (localID < FFX_PARALLELSORT_SORT_BIN_COUNT)
		{
			uint sum = 0;
			for (int i = 0; i < FFX_PARALLELSORT_THREADGROUP_SIZE; i++)
			{
				sum += gs_FFX_PARALLELSORT_Histogram[localID * FFX_PARALLELSORT_THREADGROUP_SIZE + i];
			}
			SumTable[localID * CBuffer.NumThreadGroups + groupID] = sum;
		}
	}
#endif // __cplusplus

