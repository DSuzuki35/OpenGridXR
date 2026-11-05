// Optional real-driver probe. Never part of WARP CI or an FPS benchmark.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../nv_vrs.hpp"
#include <d3dcompiler.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
static void log(void*,const char* line){std::cout<<line<<'\n';}
int main(){ std::cout<<std::unitbuf;
 ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;D3D_FEATURE_LEVEL level{};
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)))return 2;
 lmuqv::NvVrs vrs;
 if(!vrs.configure(context,64,64,2,true,lmuqv::NvVrsOwnership::ExclusiveKnownDisabled,log,nullptr)){
  std::cout<<"UNAVAILABLE: driver/device does not support the VRS probe\n";context->Release();device->Release();return 3;}
 const char* source="float4 vs(uint id:SV_VertexID):SV_Position{float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);} float4 ps(float4 p:SV_Position):SV_Target{return float4(frac(p.x/2),frac(p.y/2),0,1);}";
 ID3DBlob *vsCode=nullptr,*psCode=nullptr;
 assert(SUCCEEDED(D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&vsCode,nullptr)));
 assert(SUCCEEDED(D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&psCode,nullptr)));
 ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;
 assert(SUCCEEDED(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs)));
 assert(SUCCEEDED(device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps)));vsCode->Release();psCode->Release();
 D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=64;desc.ArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
 ID3D11Texture2D *target=nullptr,*staging=nullptr;ID3D11RenderTargetView* rtv=nullptr;
 assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&target)));assert(SUCCEEDED(device->CreateRenderTargetView(target,nullptr,&rtv)));
 desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)));
 context->OMSetRenderTargets(1,&rtv,nullptr);D3D11_VIEWPORT viewport{0,0,64,64,0,1};context->RSSetViewports(1,&viewport);
 context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(vs,nullptr,0);context->PSSetShader(ps,nullptr,0);
 auto draw=[&](){context->Draw(3,0);context->CopyResource(staging,target);D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&mapped)));
  std::vector<unsigned> pixels(64*64);for(unsigned y=0;y<64;++y)std::memcpy(pixels.data()+y*64,static_cast<const char*>(mapped.pData)+y*mapped.RowPitch,64*4);context->Unmap(staging,0);return pixels;};
 const auto baseline=draw();bool okay=true;
 for(unsigned rate:{2u,4u,1u}){
  vrs.reset();assert(draw()==baseline); // Reset must not invalidate a still-live device.
  assert(vrs.configure(context,64,64,rate,true,lmuqv::NvVrsOwnership::ExclusiveKnownDisabled,log,nullptr));
  const lmuqv::VrsFocus focus=rate==4?lmuqv::VrsFocus{.5f,0,1,.5f}:lmuqv::VrsFocus{};
  assert(vrs.update_focus(context,focus));assert(vrs.begin_outer(context));const auto coarse=draw();assert(vrs.end_outer(context));const auto restored=draw();
  unsigned changed=0,focusChanged=0;
  for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)if(baseline[y*64+x]!=coarse[y*64+x]){
   ++changed;if(x>=focus.left*64&&x<focus.right*64&&y>=focus.top*64&&y<focus.bottom*64)++focusChanged;}
  std::cout<<"hardware_vrs rate="<<rate<<" changed_pixels="<<changed<<" focus_changed="<<focusChanged<<" restored_equal="<<(baseline==restored)<<'\n';
  okay=okay&&(rate==1?changed==0:changed>0)&&focusChanged==0&&baseline==restored;
  if(rate!=1)for(unsigned step=0;step<6;++step){
   const auto moved=step%2?lmuqv::VrsFocus{}:lmuqv::VrsFocus{.5f,0,1,.5f};
   assert(vrs.update_focus(context,moved)&&vrs.begin_outer(context));const auto shifted=draw();assert(vrs.end_outer(context));
   unsigned peripheralChanges=0;
   for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)if(shifted[y*64+x]!=baseline[y*64+x]){
    ++peripheralChanges;assert(!(x>=moved.left*64&&x<moved.right*64&&y>=moved.top*64&&y<moved.bottom*64));}
   assert(peripheralChanges>0&&draw()==baseline);
  }
 }
 vrs.reset();okay=okay&&draw()==baseline;context->ClearState();staging->Release();rtv->Release();target->Release();vs->Release();ps->Release();context->Release();device->Release();
 std::cout<<"hardware_vrs cleanup_complete\n";
 return okay?0:1;
}
