// ParallelSort.h
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

#pragma once
#include <d3d12.h>

using namespace CAULDRON_DX12;

// Uncomment the following line to enable developer mode which compiles in data verification mechanism
//#define DEVELOPERMODE

struct ParallelSortRenderCB // If you change this, also change struct ParallelSortRenderCB in ParallelSortVerify.hlsl
{
    int32_t Width;
    int32_t Height;
    int32_t SortWidth;
    int32_t SortHeight;
};

// Convenience struct for passing resource/UAV pairs around
typedef struct RdxDX12ResourceInfo
{
    ID3D12Resource* pResource;          ///< Pointer to the resource -- used for barriers and syncs (must NOT be nullptr)
    D3D12_GPU_DESCRIPTOR_HANDLE resourceGPUHandle;  ///< The GPU Descriptor Handle to use for binding the resource with 32-bit stride
    D3D12_GPU_DESCRIPTOR_HANDLE resourceGPUHandle64Bit;  ///< The GPU Descriptor Handle to use for binding the resource with 64-bit stride
} RdxDX12ResourceInfo;

namespace CAULDRON_DX12
{
    class Device;
    class ResourceViewHeaps;
    class DynamicBufferRing;
    class StaticBufferPool;
}

class FFXParallelSort
{
public:
    void OnCreate(Device* pDevice, ResourceViewHeaps* pResourceViewHeaps, DynamicBufferRing* pConstantBufferRing, UploadHeap* pUploadHeap, SwapChain* pSwapChain);
    void OnDestroy();

    void Sort(ID3D12GraphicsCommandList* pCommandList, bool isBenchmarking, float benchmarkTime);
    void CopySourceDataForFrame(ID3D12GraphicsCommandList* pCommandList);
    void DrawGui();
    void DrawVisualization(ID3D12GraphicsCommandList* pCommandList, uint32_t RTWidth, uint32_t RTHeight);

    // Temp -- For command line overrides
    static void OverridePayload();
    // Temp -- For command line overrides

private:
    void CreateInterleavedKeyPayload(uint8_t* dst, const uint32_t* key, const uint32_t* payload, uint32_t size);
    void CreateKeyPayloadBuffers();
    void CompileRadixPipeline(const char* shaderFile, const DefineList* defines, const char* entryPoint, ID3D12PipelineState*& pPipeline);

    // Temp -- For command line overrides
    static bool PayloadOverride;
    // Temp -- For command line overrides

    Device*             m_pDevice = nullptr;
    UploadHeap*         m_pUploadHeap = nullptr;
    ResourceViewHeaps*  m_pResourceViewHeaps = nullptr;
    DynamicBufferRing*  m_pConstantBufferRing = nullptr;
    uint32_t            m_MaxNumThreadgroups = 320; // Use a generic thread group size when not on AMD hardware (taken from experiments to determine best performance threshold)

    // Sample resources for sorting only keys
    Texture             m_SrcKeyBuffer;     // 32 bit source key buffer
    CBV_SRV_UAV         m_SrcKeyUAVTable;       // 32 bit source key UAV

    Texture             m_DstKeyBuffer;     // 32 bit destination key buffer
    CBV_SRV_UAV         m_DstKeyUAVTable;       // 32 bit destination key UAVs

    // 64-bit sample resources for sorting key/payload
    Texture             m_SrcBuffer;     // 64 bit source key/payload buffer
    CBV_SRV_UAV         m_SrcUAVTable;       // 64 bit source key/payload UAV

    Texture             m_DstBuffer;     // 64 bit destination key/payload buffer
    CBV_SRV_UAV         m_Dst64UAVTable;       // 64 bit destination key/payload UAVs
    CBV_SRV_UAV         m_Dst32UAVTable;     // 64 bit destination key/payload UAVs with 32-bit stride

    // Resources         for parallel sort algorithm
    Texture             m_FPSScratchBuffer;             // Sort scratch buffer
    CBV_SRV_UAV         m_FPSScratchUAV;                // UAV needed for sort scratch buffer

    ID3D12RootSignature* m_pFPSRootSignature            = nullptr;
    ID3D12PipelineState* m_pFPSCountPipeline            = nullptr;
    ID3D12PipelineState* m_pFPSCountPayloadPipeline     = nullptr;

    // Resources for verification render
    ID3D12RootSignature* m_pRenderRootSignature = nullptr;
    ID3D12PipelineState* m_pRenderResultVerificationPipeline = nullptr;
    ID3D12PipelineState* m_pRenderResultVerificationPayloadPipeline = nullptr;
    Texture                 m_ValidateTexture;
    CBV_SRV_UAV             m_ValidateTextureSRV;

    // For correctness validation
    ID3D12Resource*         m_ReadBackBufferResource;           // For sort validation
    ID3D12Fence*            m_ReadBackFence;                    // To know when we can check sort results
    HANDLE                  m_ReadBackFenceEvent;

    // Options for UI and test to run
    int m_UIResolutionSize = 0;
    bool m_UISortPayload = false;
};