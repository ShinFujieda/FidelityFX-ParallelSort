// ParallelSort.cpp
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

#include "stdafx.h"
#include "../../../FFX-ParallelSort/FFX_ParallelSort.h"

#include <numeric>
#include <random>
#include <vector>

static const uint32_t NumKey_CountTest = 1920 * 1080;

//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// For doing command-line based benchmark runs
bool FFXParallelSort::PayloadOverride = false;
void FFXParallelSort::OverridePayload()
{
    PayloadOverride = true;
}
//////////////////////////////////////////////////////////////////////////

// Create all of the sort data for the sample
void FFXParallelSort::CreateInterleavedKeyPayload(uint8_t* dst, const uint32_t* keys, const uint32_t* payloads, uint32_t size)
{
    uint32_t* dst32 = (uint32_t*)dst;
    for (uint32_t i = 0; i < size; i++)
    {
        dst32[2*i] = keys[i];
        dst32[2*i+1] = payloads[i];
    }
}

// Create all of the sort data for the sample
void FFXParallelSort::CreateKeyPayloadBuffers()
{
    std::vector<uint32_t> KeyData(NumKey_CountTest);

    // Populate the buffers with linear access index
    std::iota(KeyData.begin(), KeyData.end(), 0);

    // Shuffle the data
    // std::shuffle(KeyData.begin(), KeyData.end(), std::mt19937{ std::random_device{}() });

    // 1080p
    CD3DX12_RESOURCE_DESC ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(uint32_t) * NumKey_CountTest, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    m_SrcKeyBuffer.InitBuffer(m_pDevice, "SrcKeys", &ResourceDesc, sizeof(uint32_t), D3D12_RESOURCE_STATE_COPY_DEST);
    ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(2 * sizeof(uint32_t) * NumKey_CountTest, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    m_SrcBuffer.InitBuffer(m_pDevice, "Src", &ResourceDesc, sizeof(uint32_t), D3D12_RESOURCE_STATE_COPY_DEST);

    // The DstKey buffer will be used as src/dst when sorting. A copy of the
    // source key/payload will be copied into them before hand so we can keep our original values
    ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(uint32_t) * NumKey_CountTest, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    m_DstKeyBuffer.InitBuffer(m_pDevice, "DstKeyBuf", &ResourceDesc, sizeof(uint32_t), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // The DstBuffer will be used as src/dst when sorting key and payload.
    // A copy of the source key/payload will be copied into them before hand so we can keep our original values
    ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(2 * sizeof(uint32_t) * NumKey_CountTest, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    m_DstBuffer.InitBuffer(m_pDevice, "DstBuf", &ResourceDesc, sizeof(uint32_t), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // Copy data in

    // 1080
    uint8_t* pKeyDataBuffer = m_pUploadHeap->Suballocate(NumKey_CountTest * sizeof(uint32_t), sizeof(uint32_t));
    memcpy(pKeyDataBuffer, KeyData.data() , sizeof(uint32_t) * NumKey_CountTest);
    m_pUploadHeap->GetCommandList()->CopyBufferRegion(m_SrcKeyBuffer.GetResource(), 0, m_pUploadHeap->GetResource(), pKeyDataBuffer - m_pUploadHeap->BasePtr(), sizeof(uint32_t) * NumKey_CountTest);
    uint8_t* pDataBuffer = m_pUploadHeap->Suballocate(NumKey_CountTest * sizeof(uint32_t) * 2, sizeof(uint32_t));
    CreateInterleavedKeyPayload(pDataBuffer, KeyData.data(), KeyData.data(), NumKey_CountTest); // Copy the 1k source data for payload (it doesn't matter what the payload is as we really only want it to measure cost of copying/sorting)
    m_pUploadHeap->GetCommandList()->CopyBufferRegion(m_SrcBuffer.GetResource(), 0, m_pUploadHeap->GetResource(), pDataBuffer - m_pUploadHeap->BasePtr(), 2 * sizeof(uint32_t) * NumKey_CountTest);

    // Once we are done copying the data, put in barriers to transition the source resources to 
    // copy source (which is what they will stay for the duration of app runtime)
    CD3DX12_RESOURCE_BARRIER Barriers[4] = { CD3DX12_RESOURCE_BARRIER::Transition(m_SrcKeyBuffer.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE),
                                                CD3DX12_RESOURCE_BARRIER::Transition(m_SrcBuffer.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE),

                                                // Copy the data into the dst[0] buffers for use on first frame
                                                CD3DX12_RESOURCE_BARRIER::Transition(m_DstKeyBuffer.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST),
                                                CD3DX12_RESOURCE_BARRIER::Transition(m_DstBuffer.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST) };
    m_pUploadHeap->GetCommandList()->ResourceBarrier(4, Barriers);

    m_pUploadHeap->GetCommandList()->CopyBufferRegion(m_DstKeyBuffer.GetResource(), 0, m_SrcKeyBuffer.GetResource(), 0, sizeof(uint32_t) * NumKey_CountTest);
    m_pUploadHeap->GetCommandList()->CopyBufferRegion(m_DstBuffer.GetResource(), 0, m_SrcBuffer.GetResource(), 0, 2 * sizeof(uint32_t) * NumKey_CountTest);

    // Put the dst buffers back to UAVs for sort usage
    Barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_DstKeyBuffer.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_DstBuffer.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_pUploadHeap->GetCommandList()->ResourceBarrier(2, Barriers);

    // Create UAVs
    m_SrcKeyBuffer.CreateBufferUAV(0, nullptr, &m_SrcKeyUAVTable);
    m_SrcBuffer.CreateBufferUAV(0, nullptr, &m_SrcUAVTable);
    m_DstKeyBuffer.CreateBufferUAV(0, nullptr, &m_DstKeyUAVTable);
    m_DstBuffer.CreateBufferUAV(0, nullptr, &m_Dst64UAVTable, true);
    m_DstBuffer.CreateBufferUAV(0, nullptr, &m_Dst32UAVTable);
}

// Compile specified radix sort shader and create pipeline
void FFXParallelSort::CompileRadixPipeline(const char* shaderFile, const DefineList* defines, const char* entryPoint, ID3D12PipelineState*& pPipeline)
{
    std::string CompileFlags("-T cs_6_0");
#ifdef _DEBUG
    CompileFlags += " -Zi -Od";
#endif // _DEBUG

    D3D12_SHADER_BYTECODE shaderByteCode = {};
    CompileShaderFromFile(shaderFile, defines, entryPoint, CompileFlags.c_str(), &shaderByteCode);

    D3D12_COMPUTE_PIPELINE_STATE_DESC descPso = {};
    descPso.CS = shaderByteCode;
    descPso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    descPso.pRootSignature = m_pFPSRootSignature;
    descPso.NodeMask = 0;

    ThrowIfFailed(m_pDevice->GetDevice()->CreateComputePipelineState(&descPso, IID_PPV_ARGS(&pPipeline)));
    SetName(pPipeline, entryPoint);
}

// Parallel Sort initialization
void FFXParallelSort::OnCreate(Device* pDevice, ResourceViewHeaps* pResourceViewHeaps, DynamicBufferRing* pConstantBufferRing, UploadHeap* pUploadHeap, SwapChain* pSwapChain)
{
    m_pDevice = pDevice;
    m_pUploadHeap = pUploadHeap;
    m_pResourceViewHeaps = pResourceViewHeaps;
    m_pConstantBufferRing = pConstantBufferRing;
    m_MaxNumThreadgroups = 800;

    // Overrides for testing
    if (PayloadOverride)
        m_UISortPayload = true;

    // Allocate UAVs to use for data
    m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_SrcKeyUAVTable);
    m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_SrcUAVTable);
    m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_DstKeyUAVTable);
    m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_Dst64UAVTable);
    m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_Dst32UAVTable);
    m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_FPSScratchUAV);
    m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_ValidateTextureSRV);

    // Create resources to test with
    CreateKeyPayloadBuffers();

    // Create resources for sort validation
    m_ValidateTexture.InitFromFile(m_pDevice, m_pUploadHeap, "Validate1080p.png", false, 1.f, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );
    m_ValidateTexture.CreateSRV(0, &m_ValidateTextureSRV, 0);

    // Finish up
    m_pUploadHeap->FlushAndFinish();

    // Allocate the scratch buffers needed for radix sort
    uint32_t scratchBufferSize;
    uint32_t reducedScratchBufferSize;
    FFX_ParallelSort_CalculateScratchResourceSize(NumKey_CountTest, scratchBufferSize, reducedScratchBufferSize);

    CD3DX12_RESOURCE_DESC ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(scratchBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    m_FPSScratchBuffer.InitBuffer(m_pDevice, "Scratch", &ResourceDesc, sizeof(uint32_t), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_FPSScratchBuffer.CreateBufferUAV(0, nullptr, &m_FPSScratchUAV);

    // Create root signature for Radix sort passes
    {
        D3D12_DESCRIPTOR_RANGE descRange[3];
        D3D12_ROOT_PARAMETER rootParams[4];

        // Constant buffer table (always have 1)
        descRange[0] = { D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
        rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParams[0].Descriptor = { descRange[0].BaseShaderRegister, descRange[0].RegisterSpace };

        rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParams[1].Constants = { 1, 0, 1 };

        // SrcBuffer (sort or scan)
        descRange[1] = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
        rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParams[2].DescriptorTable = { 1, &descRange[1] };

        // Scratch (sort only)
        descRange[2] = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 1, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
        rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParams[3].DescriptorTable = { 1, &descRange[2] };

        D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
        rootSigDesc.NumParameters = 4;
        rootSigDesc.pParameters = rootParams;
        rootSigDesc.NumStaticSamplers = 0;
        rootSigDesc.pStaticSamplers = nullptr;
        rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ID3DBlob* pOutBlob, * pErrorBlob = nullptr;
        ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
        ThrowIfFailed(pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&m_pFPSRootSignature)));
        SetName(m_pFPSRootSignature, "FPS_Signature");

        pOutBlob->Release();
        if (pErrorBlob)
            pErrorBlob->Release();
    }

    // Create root signature for Render of RadixBuffer info
    {
        CD3DX12_DESCRIPTOR_RANGE    DescRange[3];
        CD3DX12_ROOT_PARAMETER      RTSlot[3];

        // Constant buffer
        DescRange[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0);
        RTSlot[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

        // UAV for RadixBufer
        DescRange[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);
        RTSlot[1].InitAsDescriptorTable(1, &DescRange[1], D3D12_SHADER_VISIBILITY_ALL);

        // SRV for Validation texture
        DescRange[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
        RTSlot[2].InitAsDescriptorTable(1, &DescRange[2], D3D12_SHADER_VISIBILITY_ALL);

        CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
        descRootSignature.NumParameters = 3;
        descRootSignature.pParameters = RTSlot;
        descRootSignature.NumStaticSamplers = 0;
        descRootSignature.pStaticSamplers = nullptr;
        descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ID3DBlob* pOutBlob, * pErrorBlob = nullptr;
        ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
        ThrowIfFailed(pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&m_pRenderRootSignature)));
        SetName(m_pRenderRootSignature, "FPS_RenderResults_Signature");

        pOutBlob->Release();
        if (pErrorBlob)
            pErrorBlob->Release();
    }

    //////////////////////////////////////////////////////////////////////////
    // Create pipelines for radix sort
    {
        // Create all of the necessary pipelines for Sort and Scan

        // Radix count (sum table generation)
        CompileRadixPipeline("ParallelSortCS.hlsl", nullptr, "FPS_Count", m_pFPSCountPipeline);

        // Radix count with payload (key and payload redistribution)
        DefineList defines;
        defines["kRS_ValueCopy"] = std::to_string(1);
        CompileRadixPipeline("ParallelSortCS.hlsl", &defines, "FPS_Count", m_pFPSCountPayloadPipeline);
    }

    //////////////////////////////////////////////////////////////////////////
    // Create pipelines for render pass
    {
#ifdef _DEBUG
        std::string CompileFlagsVS("-T vs_6_0 -Zi -Od");
        std::string CompileFlagsPS("-T ps_6_0 -Zi -Od");
#else
        std::string CompileFlagsVS("-T vs_6_0");
        std::string CompileFlagsPS("-T ps_6_0");
#endif // _DEBUG

        D3D12_SHADER_BYTECODE shaderByteCodeVS = {};
        CompileShaderFromFile("ParallelSortVerify.hlsl", nullptr, "FullscreenVS", CompileFlagsVS.c_str(), &shaderByteCodeVS);

        D3D12_SHADER_BYTECODE shaderByteCodePS = {};
        CompileShaderFromFile("ParallelSortVerify.hlsl", nullptr, "RenderSortValidationPS", CompileFlagsPS.c_str(), &shaderByteCodePS);

        D3D12_SHADER_BYTECODE shaderByteCodePSPayload = {};
        {
            // Sorted with payload
            DefineList defines;
            defines["kRSV_Payload"] = std::to_string(1);
            CompileShaderFromFile("ParallelSortVerify.hlsl", &defines, "RenderSortValidationPS", CompileFlagsPS.c_str(), &shaderByteCodePSPayload);
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC descPso = {};
        descPso.InputLayout = { nullptr, 0 };
        descPso.pRootSignature = m_pRenderRootSignature;
        descPso.VS = shaderByteCodeVS;
        descPso.PS = shaderByteCodePS;
        descPso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        descPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        descPso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        descPso.BlendState.RenderTarget[0].BlendEnable = FALSE;
        descPso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        descPso.DepthStencilState.DepthEnable = FALSE;
        descPso.SampleMask = UINT_MAX;
        descPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        descPso.NumRenderTargets = 1;
        descPso.RTVFormats[0] = pSwapChain->GetFormat();
        descPso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        descPso.SampleDesc.Count = 1;
        descPso.NodeMask = 0;
        ThrowIfFailed(m_pDevice->GetDevice()->CreateGraphicsPipelineState(&descPso, IID_PPV_ARGS(&m_pRenderResultVerificationPipeline)));
        SetName(m_pRenderResultVerificationPipeline, "RenderFPSResults_Pipeline");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC descPsoPayload = {};
        descPsoPayload.InputLayout = { nullptr, 0 };
        descPsoPayload.pRootSignature = m_pRenderRootSignature;
        descPsoPayload.VS = shaderByteCodeVS;
        descPsoPayload.PS = shaderByteCodePSPayload;
        descPsoPayload.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        descPsoPayload.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        descPsoPayload.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        descPsoPayload.BlendState.RenderTarget[0].BlendEnable = FALSE;
        descPsoPayload.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        descPsoPayload.DepthStencilState.DepthEnable = FALSE;
        descPsoPayload.SampleMask = UINT_MAX;
        descPsoPayload.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        descPsoPayload.NumRenderTargets = 1;
        descPsoPayload.RTVFormats[0] = pSwapChain->GetFormat();
        descPsoPayload.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        descPsoPayload.SampleDesc.Count = 1;
        descPsoPayload.NodeMask = 0;
        ThrowIfFailed(m_pDevice->GetDevice()->CreateGraphicsPipelineState(&descPsoPayload, IID_PPV_ARGS(&m_pRenderResultVerificationPayloadPipeline)));
        SetName(m_pRenderResultVerificationPayloadPipeline, "RenderFPSResultsPayload_Pipeline");
    }
}

// Parallel Sort termination
void FFXParallelSort::OnDestroy()
{
    // Release verification render resources
    m_pRenderResultVerificationPipeline->Release();
    m_pRenderResultVerificationPayloadPipeline->Release();
    m_pRenderRootSignature->Release();
    m_ValidateTexture.OnDestroy();

    // Release radix sort algorithm resources
    m_FPSScratchBuffer.OnDestroy();
    m_pFPSRootSignature->Release();
    m_pFPSCountPipeline->Release();
    m_pFPSCountPayloadPipeline->Release();

    // Release all of our resources
    m_SrcKeyBuffer.OnDestroy();
    m_SrcBuffer.OnDestroy();
    m_DstKeyBuffer.OnDestroy();
    m_DstBuffer.OnDestroy();
}

// Because we are sorting the data every frame, need to reset to unsorted version of data before running sort
void FFXParallelSort::CopySourceDataForFrame(ID3D12GraphicsCommandList* pCommandList)
{
    // Copy the contents the source buffer to the dstBuffer[0] each frame in order to not lose our original data

    // Copy the data into the dst[0] buffers for use on first frame
    CD3DX12_RESOURCE_BARRIER Barriers[2] = { CD3DX12_RESOURCE_BARRIER::Transition(m_DstKeyBuffer.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST),
                                                CD3DX12_RESOURCE_BARRIER::Transition(m_DstBuffer.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST) };
    pCommandList->ResourceBarrier(2, Barriers);

    pCommandList->CopyBufferRegion(m_DstKeyBuffer.GetResource(), 0, m_SrcKeyBuffer.GetResource(), 0, sizeof(uint32_t) * NumKey_CountTest);
    pCommandList->CopyBufferRegion(m_DstBuffer.GetResource(), 0, m_SrcBuffer.GetResource(), 0, 2 * sizeof(uint32_t) * NumKey_CountTest);

    // Put the dst buffers back to UAVs for sort usage
    Barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_DstKeyBuffer.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_DstBuffer.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    pCommandList->ResourceBarrier(2, Barriers);
}

// Perform Parallel Sort (radix-based sort)
void FFXParallelSort::Sort(ID3D12GraphicsCommandList* pCommandList, bool isBenchmarking, float benchmarkTime)
{
    std::string markerText = "FFXParallelSort";
    UserMarker marker(pCommandList, markerText.c_str());

    FFX_ParallelSortCB  constantBufferData = { 0 };

    // Bind the descriptor heaps
    ID3D12DescriptorHeap* pDescriptorHeap = m_pResourceViewHeaps->GetCBV_SRV_UAVHeap();
    pCommandList->SetDescriptorHeaps(1, &pDescriptorHeap);

    // Bind the root signature
    pCommandList->SetComputeRootSignature(m_pFPSRootSignature);

    // Fill in the constant buffer data structure (this will be done by a shader in the indirect version)
    uint32_t NumThreadgroupsToRun;
    uint32_t NumReducedThreadgroupsToRun;
    {
        uint32_t NumberOfKeys = NumKey_CountTest;
        FFX_ParallelSort_SetConstantAndDispatchData(NumberOfKeys, m_MaxNumThreadgroups, constantBufferData, NumThreadgroupsToRun, NumReducedThreadgroupsToRun);
    }

    // Setup resource/UAV pairs to use during sort
    bool bHasPayload = m_UISortPayload;
    RdxDX12ResourceInfo SrcInfo = { m_DstKeyBuffer.GetResource(), m_DstKeyUAVTable.GetGPU(0), 0 };
    if (bHasPayload)
    {
        SrcInfo = { m_DstBuffer.GetResource(), m_Dst32UAVTable.GetGPU(0), m_Dst64UAVTable.GetGPU(0) };
    }
    RdxDX12ResourceInfo ScratchBufferInfo = { m_FPSScratchBuffer.GetResource(), m_FPSScratchUAV.GetGPU(), 0 };

    // Buffers to ping-pong between when writing out sorted values
    const RdxDX12ResourceInfo *ReadBufferInfo(&SrcInfo);

    // Setup barriers for the run
    CD3DX12_RESOURCE_BARRIER barrier[1];

    // Perform Radix Sort. here only execute the count stage
    uint32_t Shift = 0;
    // for (uint32_t Shift = 0; Shift < 32u; Shift += FFX_PARALLELSORT_SORT_BITS_PER_PASS)
    {
        // Update the bit shift
        pCommandList->SetComputeRoot32BitConstant(1, Shift, 0);

        // Copy the data into the constant buffer
        D3D12_GPU_VIRTUAL_ADDRESS constantBuffer = m_pConstantBufferRing->AllocConstantBuffer(sizeof(FFX_ParallelSortCB), &constantBufferData);

        // Bind to root signature
        pCommandList->SetComputeRootConstantBufferView(0, constantBuffer);                      // Constant buffer
        pCommandList->SetComputeRootDescriptorTable(2, ReadBufferInfo->resourceGPUHandle);      // SrcBuffer
        pCommandList->SetComputeRootDescriptorTable(3, ScratchBufferInfo.resourceGPUHandle);    // Scratch buffer

        // Sort Count
        {
            // pCommandList->SetPipelineState(m_pFPSCountPipeline);
            pCommandList->SetPipelineState(bHasPayload ? m_pFPSCountPayloadPipeline : m_pFPSCountPipeline);

            pCommandList->Dispatch(NumThreadgroupsToRun, 1, 1);
        }

        // UAV barrier on the sum table
        barrier[0] = CD3DX12_RESOURCE_BARRIER::UAV(ScratchBufferInfo.pResource);
        pCommandList->ResourceBarrier(1, barrier);
    }
}

// Render Parallel Sort related GUI
void FFXParallelSort::DrawGui()
{
    if (ImGui::CollapsingHeader("FFX Parallel Sort", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImVec2 textSize = ImGui::CalcTextSize("1920x1080");

        ImGui::Checkbox("Sort Payload", &m_UISortPayload);
    }
}

// Renders the image with the sorted/unsorted indicies for visual representation
void FFXParallelSort::DrawVisualization(ID3D12GraphicsCommandList* pCommandList, uint32_t RTWidth, uint32_t RTHeight)
{
    // Setup the constant buffer
    ParallelSortRenderCB ConstantBuffer;
    ConstantBuffer.Width = RTWidth;
    ConstantBuffer.Height = RTHeight;
    static const uint32_t SortWidth = 1920;
    static const uint32_t SortHeight = 1080;
    ConstantBuffer.SortWidth = SortWidth;
    ConstantBuffer.SortHeight = SortHeight;

    // Bind root signature and descriptor heaps
    ID3D12DescriptorHeap* pDescriptorHeap = m_pResourceViewHeaps->GetCBV_SRV_UAVHeap();
    pCommandList->SetDescriptorHeaps(1, &pDescriptorHeap);
    pCommandList->SetGraphicsRootSignature(m_pRenderRootSignature);

    // Bind constant buffer
    D3D12_GPU_VIRTUAL_ADDRESS GPUCB = m_pConstantBufferRing->AllocConstantBuffer(sizeof(ParallelSortRenderCB), &ConstantBuffer);
    pCommandList->SetGraphicsRootConstantBufferView(0, GPUCB);

    // If we are showing unsorted values, need to transition the source data buffer from copy source to UAV and back
    if (!m_UISortPayload)
        pCommandList->SetGraphicsRootDescriptorTable(1, m_DstKeyUAVTable.GetGPU());
    else
        pCommandList->SetGraphicsRootDescriptorTable(1, m_Dst64UAVTable.GetGPU());

    // Bind validation texture
    pCommandList->SetGraphicsRootDescriptorTable(2, m_ValidateTextureSRV.GetGPU());

    D3D12_VIEWPORT vp = {};
    vp.Width = (float)RTWidth;
    vp.Height = (float)RTHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = vp.TopLeftY = 0.0f;
    pCommandList->RSSetViewports(1, &vp);

    // Set the shader and dispatch
    pCommandList->IASetVertexBuffers(0, 0, nullptr);
    pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    pCommandList->SetPipelineState(m_UISortPayload ? m_pRenderResultVerificationPayloadPipeline : m_pRenderResultVerificationPipeline);
    pCommandList->DrawInstanced(3, 1, 0, 0);
}
