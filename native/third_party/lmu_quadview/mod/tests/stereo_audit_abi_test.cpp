#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define CINTERFACE
#define D3D11_NO_HELPERS
#include "../stereo_audit.hpp"
#include <cstddef>
#include <iostream>
#define CONTEXT(I,Method) static_assert(lmuqv::stereo_audit_vtable[I]==offsetof(ID3D11DeviceContextVtbl,Method)/sizeof(void*))
#define DEVICE(I,Method) static_assert(lmuqv::stereo_audit_vtable[I]==offsetof(ID3D11DeviceVtbl,Method)/sizeof(void*))
CONTEXT(0,DrawIndexed);CONTEXT(1,Draw);CONTEXT(2,DrawIndexedInstanced);CONTEXT(3,DrawInstanced);
CONTEXT(4,DrawAuto);CONTEXT(5,DrawIndexedInstancedIndirect);CONTEXT(6,DrawInstancedIndirect);
CONTEXT(7,Dispatch);CONTEXT(8,DispatchIndirect);CONTEXT(9,Map);CONTEXT(10,Unmap);
CONTEXT(11,UpdateSubresource);CONTEXT(12,CopyResource);CONTEXT(13,CopySubresourceRegion);CONTEXT(21,ExecuteCommandList);
CONTEXT(22,Begin);CONTEXT(23,End);
DEVICE(14,CreateVertexShader);DEVICE(15,CreatePixelShader);DEVICE(16,CreateGeometryShader);DEVICE(17,CreateHullShader);DEVICE(18,CreateDomainShader);DEVICE(19,CreateComputeShader);DEVICE(20,CreateInputLayout);
int main(){std::cout<<"Stereo audit vtable ABI verified against the SDK\n";}
