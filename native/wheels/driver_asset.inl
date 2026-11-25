// Included by mesh_renderer.cpp in namespace ovrlay so the wheel and driver share projection and depth shaders.
struct DriverVertex {
    float position[3],normal[3],uv[2],part;
    uint32_t joints[4];
    float weights[4];
};
struct DriverGpuVertex { DriverVertex mesh; float followGrip; };
static_assert(sizeof(DriverGpuVertex)==72);
struct DriverHeader {
    char magic[8];
    uint32_t version,vertices,indices,joints,frames,width,height;
};
static_assert(sizeof(DriverHeader)==36 && sizeof(DriverVertex)==68);

struct DriverAsset {
    struct Pose {float joints[128*12]{},mount[12]{},gripMount[12]{},glove[4]{},suit[4]{};} pose;
    Com<ID3D11Buffer> vertices,indices,constants;
    Com<ID3D11ShaderResourceView> atlas,normalMap;
    Com<ID3D11VertexShader> vertex;
    Com<ID3D11PixelShader> pixel,emission;
    Com<ID3D11InputLayout> layout;
    std::vector<float> palette;
    float bounds[241][4][6]{},currentBounds[4][6]{};
    unsigned joints{},counts[4]{};
    bool torso{},legs{},dirty{},texturedCloth{};

    bool initialize(ID3D11Device* device,const std::wstring& path) {
        // The host can replace staged assets while a prior revision finishes loading.
        HANDLE handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,
                                  OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
        if(handle==INVALID_HANDLE_VALUE)return false;
        int fd=_open_osfhandle(reinterpret_cast<intptr_t>(handle),_O_RDONLY|_O_BINARY);
        if(fd<0){CloseHandle(handle);return false;}
        std::unique_ptr<FILE,decltype(&fclose)> file(_fdopen(fd,"rb"),fclose);
        if(!file){_close(fd);return false;}
        DriverHeader h{};
        if(!file || fread(&h,sizeof(h),1,file.get())!=1 || memcmp(h.magic,"AGDRVR1",8) ||
           (h.version<2 || h.version>4) || !h.vertices || h.vertices>300000 || !h.indices || h.indices>1200000 ||
           h.indices%3 || !h.joints || h.joints>128 || h.frames!=241 || !h.width || !h.height ||
           h.width>2048 || h.height>2048) return false;
        if(h.version>=4 && (h.width<2 || h.width%2))return false;
        const size_t paletteSize=size_t(h.frames)*h.joints*12,atlasSize=size_t(h.width)*h.height*4;
        const size_t bytes=sizeof(h)+h.vertices*sizeof(DriverVertex)+h.indices*sizeof(uint32_t)+
                           paletteSize*sizeof(float)+sizeof(bounds)+atlasSize*(h.version>=3?2:1);
        if(_filelengthi64(_fileno(file.get()))!=static_cast<__int64>(bytes)) return false;
        std::vector<DriverVertex> verts(h.vertices);
        std::vector<uint32_t> input(h.indices),sorted(h.indices);
        std::vector<unsigned char> texture(atlasSize);
        std::vector<unsigned char> normalTexture(atlasSize);
        palette.resize(paletteSize);joints=h.joints;texturedCloth=h.version>=4;
        auto read=[&](void* target,size_t size){return fread(target,1,size,file.get())==size;};
        if(!read(verts.data(),verts.size()*sizeof(DriverVertex)) || !read(input.data(),input.size()*4) ||
           !read(palette.data(),palette.size()*4) || !read(bounds,sizeof(bounds)) ||
           !read(texture.data(),atlasSize) || (h.version>=3 && !read(normalTexture.data(),atlasSize))) return false;
        if(h.version==2)for(size_t i=0;i<atlasSize;i+=4) {
            normalTexture[i]=normalTexture[i+1]=128;normalTexture[i+2]=normalTexture[i+3]=255;
        }
        auto bounded=[](float v){return std::isfinite(v) && std::abs(v)<=100;};
        for(auto& v:verts) {
            for(float q:v.position) if(!bounded(q)) return false;
            for(float q:v.normal) if(!bounded(q)) return false;
            for(float q:v.uv) if(!bounded(q)) return false;
            if(!bounded(v.part) || v.part<0 || v.part>3 || v.part!=std::floor(v.part)) return false;
            float sum=0;
            for(unsigned k=0;k<4;k++) {
                if(v.joints[k]>=joints || !bounded(v.weights[k]) || v.weights[k]<0 || v.weights[k]>1) return false;
                sum+=v.weights[k];
            }
            if(std::abs(sum-1)>.002f) return false;
        }
        for(float v:palette) if(!bounded(v)) return false;
        for(auto& frame:bounds) for(auto& b:frame) for(unsigned a=0;a<3;a++)
            if(!bounded(b[a]) || !bounded(b[a+3]) || b[a]>b[a+3]) return false;
        for(uint32_t i:input) if(i>=h.vertices) return false;
        for(size_t i=0;i<input.size();i+=3) {
            const float part=verts[input[i]].part;
            if(verts[input[i+1]].part!=part || verts[input[i+2]].part!=part) return false;
            counts[unsigned(part)]+=3;
        }
        unsigned offsets[]={0,counts[0],counts[0]+counts[1],counts[0]+counts[1]+counts[2]};
        for(size_t i=0;i<input.size();i+=3) {
            auto& at=offsets[unsigned(verts[input[i]].part)];
            std::copy_n(input.data()+i,3,sorted.data()+at);at+=3;
        }
        auto buffer=[&](UINT size,UINT bind,const void* initial,Com<ID3D11Buffer>& out){
            D3D11_BUFFER_DESC b{};b.ByteWidth=size;b.BindFlags=bind;
            b.Usage=initial?D3D11_USAGE_IMMUTABLE:D3D11_USAGE_DEFAULT;
            D3D11_SUBRESOURCE_DATA data{};data.pSysMem=initial;
            return SUCCEEDED(device->CreateBuffer(&b,initial?&data:nullptr,out.put()));
        };
        // Use the rest pose to weight sleeve vertices from the wrist seam to the
        // stationary shoulder. Bind positions use the author's individual mesh spaces.
        std::vector<DriverGpuVertex> gpu(verts.size());
        const float* neutral=palette.data()+120*joints*12;
        float wrist=100,shoulder=-100;
        for(size_t i=0;i<verts.size();i++) {
            const auto& v=verts[i];gpu[i].mesh=v;
            if(v.part!=1){gpu[i].followGrip=v.part==0?1.f:0.f;continue;}
            float z=0;
            for(unsigned k=0;k<4;k++) {
                const float* r=neutral+v.joints[k]*12+8;
                z+=v.weights[k]*(r[0]*v.position[0]+r[1]*v.position[1]+r[2]*v.position[2]+r[3]);
            }
            gpu[i].followGrip=z;wrist=std::min(wrist,z);shoulder=std::max(shoulder,z);
        }
        for(auto& v:gpu)if(v.mesh.part==1) {
            const float t=std::clamp(((v.followGrip-wrist)/std::max(.001f,shoulder-wrist)-.12f)/.74f,0.f,1.f);
            v.followGrip=1.f-t*t*(3.f-2.f*t);
        }
        if(!buffer(UINT(gpu.size()*sizeof(DriverGpuVertex)),D3D11_BIND_VERTEX_BUFFER,gpu.data(),vertices) ||
           !buffer(UINT(sorted.size()*4),D3D11_BIND_INDEX_BUFFER,sorted.data(),indices) ||
           !buffer(sizeof(Pose),D3D11_BIND_CONSTANT_BUFFER,nullptr,constants)) return false;
        // Build filtered mip chains on the asset worker. Never ask the game's
        // immediate context to generate mips or upload changing vertex data.
        auto makeTexture=[&](const std::vector<unsigned char>& base,bool normal,Com<ID3D11ShaderResourceView>& view) {
            // Packed file regions become distinct array slices. Each slice has
            // its own mip pyramid, so glove color/alpha cannot become cloth
            // stitching at distance, even in the final 1x1 mip.
            const unsigned slices=texturedCloth?2u:1u,textureWidth=h.width/slices;
            std::vector<std::vector<unsigned char>> levels;
            std::vector<unsigned> widths;
            unsigned mipCount=0;
            float linear[256];for(unsigned i=0;i<256;i++) {
                const float v=float(i)/255;linear[i]=v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);
            }
            for(unsigned slice=0;slice<slices;slice++) {
              const size_t first=levels.size();
              std::vector<unsigned char> region(size_t(textureWidth)*h.height*4);
              for(unsigned y=0;y<h.height;y++)std::copy_n(base.data()+(size_t(y)*h.width+slice*textureWidth)*4,
                  textureWidth*4,region.data()+size_t(y)*textureWidth*4);
              levels.push_back(std::move(region));widths.push_back(textureWidth);
              unsigned w=textureWidth,height=h.height;
              while(w>1 || height>1) {
                const unsigned nw=std::max(1u,w/2),nh=std::max(1u,height/2);
                const auto& previous=levels.back();std::vector<unsigned char> next(size_t(nw)*nh*4);
                for(unsigned y=0;y<nh;y++)for(unsigned x=0;x<nw;x++) {
                    float value[4]{};
                    for(unsigned dy=0;dy<2;dy++)for(unsigned dx=0;dx<2;dx++) {
                        const size_t at=(size_t(std::min(height-1,2*y+dy))*w+std::min(w-1,2*x+dx))*4;
                        for(unsigned k=0;k<4;k++)value[k]+=(normal||k==3)?float(previous[at+k])/255:linear[previous[at+k]];
                    }
                    for(float& v:value)v*=.25f;
                    if(normal) {
                        float n[]={value[0]*2-1,value[1]*2-1,value[2]*2-1};
                        const float length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
                        for(unsigned k=0;k<3;k++)value[k]=(length>1e-8f?n[k]/length:(k==2?1.f:0.f))*.5f+.5f;
                    } else for(unsigned k=0;k<3;k++)value[k]=value[k]<=.0031308f?value[k]*12.92f:1.055f*std::pow(value[k],1/2.4f)-.055f;
                    for(unsigned k=0;k<4;k++)next[(size_t(y)*nw+x)*4+k]=static_cast<unsigned char>(std::clamp(value[k]*255+.5f,0.f,255.f));
                }
                levels.push_back(std::move(next));widths.push_back(nw);w=nw;height=nh;
              }
              mipCount=unsigned(levels.size()-first);
            }
            std::vector<D3D11_SUBRESOURCE_DATA> data;
            for(size_t i=0;i<levels.size();i++)data.push_back({levels[i].data(),widths[i]*4,0});
            D3D11_TEXTURE2D_DESC desc{};desc.Width=textureWidth;desc.Height=h.height;
            desc.MipLevels=mipCount;desc.ArraySize=slices;desc.SampleDesc.Count=1;
            desc.Format=normal?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            Com<ID3D11Texture2D> tex;
            D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=desc.Format;
            srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            srv.Texture2DArray.MipLevels=mipCount;srv.Texture2DArray.ArraySize=slices;
            return SUCCEEDED(device->CreateTexture2D(&desc,data.data(),tex.put())) &&
                   SUCCEEDED(device->CreateShaderResourceView(tex.get(),&srv,view.put()));
        };
        if(!makeTexture(texture,false,atlas) || !makeTexture(normalTexture,true,normalMap))return false;

        const auto library=LoadLibraryExW(L"d3dcompiler_47.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!library)return false;
        decltype(&D3DCompile) compile{};
        const auto address=GetProcAddress(library,"D3DCompile");std::memcpy(&compile,&address,sizeof(compile));
        D3D_SHADER_MACRO macros[]={{"DRIVER","1"},{"SHADING_FREQUENCY","centroid"},{nullptr,nullptr}};
        Com<ID3DBlob> blob;
        auto make=[&](const char* entry,const char* profile){
            return compile && SUCCEEDED(compile(shader,sizeof(shader)-1,"OpenGridDriver",macros,nullptr,entry,profile,
                                                  D3DCOMPILE_OPTIMIZATION_LEVEL3,0,blob.put(),nullptr));
        };
        D3D11_INPUT_ELEMENT_DESC elements[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",1,DXGI_FORMAT_R32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"BLENDINDICES",0,DXGI_FORMAT_R32G32B32A32_UINT,0,36,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"BLENDWEIGHT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,52,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",2,DXGI_FORMAT_R32_FLOAT,0,68,D3D11_INPUT_PER_VERTEX_DATA,0}};
        const bool ok=make("skinVS","vs_5_0") &&
            SUCCEEDED(device->CreateVertexShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,vertex.put())) &&
            SUCCEEDED(device->CreateInputLayout(elements,7,blob->GetBufferPointer(),blob->GetBufferSize(),layout.put())) &&
            make("ps","ps_5_0") && SUCCEEDED(device->CreatePixelShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,pixel.put())) &&
            make("psEmission","ps_5_0") && SUCCEEDED(device->CreatePixelShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,emission.put()));
        blob.reset();FreeLibrary(library);return ok;
    }

    void draw(ID3D11DeviceContext* ctx,bool bloom) {
        if(dirty) {ctx->UpdateSubresource(constants.get(),0,nullptr,&pose,0,0);dirty=false;}
        auto* cb=constants.get();
        ctx->VSSetConstantBuffers(2,1,&cb);ctx->PSSetConstantBuffers(2,1,&cb);
        ctx->IASetInputLayout(layout.get());ctx->VSSetShader(vertex.get(),nullptr,0);
        ctx->PSSetShader(bloom?emission.get():pixel.get(),nullptr,0);
        auto* source=atlas.get();ctx->PSSetShaderResources(0,1,&source);
        source=normalMap.get();ctx->PSSetShaderResources(4,1,&source);
        auto* vb=vertices.get();UINT stride=sizeof(DriverGpuVertex),offset=0;
        ctx->IASetVertexBuffers(0,1,&vb,&stride,&offset);ctx->IASetIndexBuffer(indices.get(),DXGI_FORMAT_R32_UINT,0);
        unsigned upper=counts[0]+counts[1]+(torso?counts[2]:0);
        ctx->DrawIndexed(upper,0,0);
        if(legs && counts[3])ctx->DrawIndexed(counts[3],counts[0]+counts[1]+counts[2],0);
    }
};
bool MeshRenderer::selectDriver(ID3D11Device* device,unsigned slot,const std::wstring& path,float radians,
                                const float (&mount)[12],unsigned glove,unsigned suit,bool torso,bool legs) noexcept {
    driver=nullptr;
    if(slot>=15 || !device || !active || !std::isfinite(radians))return false;
    for(float v:mount)if(!std::isfinite(v))return false;
    try {
        driver=drivers.select(slot,path,[&]{
            return std::async(std::launch::async,[retained=Microsoft::WRL::ComPtr<ID3D11Device>(device),path]{
                auto a=std::make_shared<DriverAsset>();
                if(!a->initialize(retained.Get(),path))a.reset();
                return a;
            });
        });
        if(!driver)return false;
        if(!driverTargets)driverTargets=std::make_unique<MeshTargets>();
        auto& d=*driver;d.torso=torso;d.legs=legs;d.dirty=true;
        const float degrees=radians*(180.f/3.14159265359f);
        const float sampled=std::abs(degrees)>360.f?std::fmod(degrees,180.f):degrees;
        const float frame=std::clamp(sampled/3.f+120.f,0.f,240.f);
        const unsigned a=unsigned(frame),b=std::min(a+1,240u),stride=d.joints*12;
        for(unsigned i=0;i<stride;i++) {
            const float x=d.palette[a*stride+i];
            d.pose.joints[i]=x+(frame-float(a))*(d.palette[b*stride+i]-x);
        }
        for(unsigned part=0;part<4;part++)for(unsigned i=0;i<3;i++) {
            d.currentBounds[part][i]=std::min(d.bounds[a][part][i],d.bounds[b][part][i]);
            d.currentBounds[part][i+3]=std::max(d.bounds[a][part][i+3],d.bounds[b][part][i+3]);
        }
        std::copy_n(mount,12,d.pose.mount);
        const float c=std::cos(radians),s=std::sin(radians);
        const float back[]={c,s,0,0,-s,c,0,0,0,0,1,0};
        std::copy_n(back,12,d.pose.gripMount);
        constexpr float tint[][4]={{.36f,.035f,.72f,1},{.04f,.04f,.045f,1},{1,0,0,1},{.025f,.17f,.8f,1},{.9f,.9f,.9f,1}};
        std::copy_n(tint[glove<=4?glove:0],4,d.pose.glove);
        std::copy_n(tint[suit<=4?suit:0],4,d.pose.suit);
        d.pose.suit[3]=d.texturedCloth?1.f:0.f;
        return true;
    } catch(...) {driver=nullptr;return false;}
}
