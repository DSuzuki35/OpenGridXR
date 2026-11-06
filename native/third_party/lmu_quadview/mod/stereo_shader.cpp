#include "stereo_shader.hpp"
#include "dxbc_shader_internal.hpp"

namespace lmuqv {namespace {
std::string geometry_source(const std::vector<Parameter>& params){
    std::string fields;std::set<unsigned> registers;
    for(size_t i=0;i<params.size();++i){const auto& p=params[i];const auto mask=p.masks&15;
        require((mask==1||mask==3||mask==7||mask==15)&&registers.insert(p.reg).second,"geometry fallback requires unshared, contiguous output components");
        require(p.system==0||p.system==1,"unsupported geometry system semantic");
        const auto n=mask==1?1:mask==3?2:mask==7?3:4;
        fields+=(p.type!=3?"nointerpolation ":"");fields+=p.type==1?"uint":p.type==2?"int":"float";if(n>1)fields+=std::to_string(n);
        fields+=" v"+std::to_string(i)+":"+p.semantic+(p.index?std::to_string(p.index):"")+";";
    }
    std::string source="struct In{"+fields+"};struct Out{"+fields+"uint slice:SV_RenderTargetArrayIndex;};[maxvertexcount(3)]void main(triangle In a[3],inout TriangleStream<Out> stream){for(uint j=0;j<3;++j){Out o;";
    size_t eye=params.size();for(size_t i=0;i<params.size();++i){source+="o.v"+std::to_string(i)+"=a[j].v"+std::to_string(i)+";";if(params[i].semantic==eyeSemantic)eye=i;}
    require(eye<params.size(),"missing eye semantic");return source+"o.slice=a[j].v"+std::to_string(eye)+";stream.Append(o);}}";
}
} // namespace
bool convert_stereo_shader(const void* raw,size_t size,StereoShaderStage stage,uint32_t slots,bool direct,StereoShaderVariant& result,std::string& error){
    result={};error.clear();try{
        require(slots&&!(slots&~0x1fffu),"eye-dependent CB selection must use slots 0-12");auto chunks=read_container(raw,size);
        std::vector<Parameter> inputs,outputs;Words code;
        for(const auto& chunk:chunks){if(chunk.type==ISGN)inputs=read_signature(chunk.data);else if(chunk.type==OSGN)outputs=read_signature(chunk.data);
            else if(chunk.type==SHDR){require(chunk.data.size()%4==0,"unaligned shader tokens");code.resize(chunk.data.size()/4);std::memcpy(code.data(),chunk.data.data(),chunk.data.size());}
        }
        require(!inputs.empty()&&!outputs.empty()&&!code.empty(),"SM4 input/output signatures and SHDR required");
        require(code[0]==(stage==StereoShaderStage::vertex?0x10040u:0x40u),"initial converter supports SM4.0 VS/PS only");
        for(const auto& p:inputs)require(p.semantic!=eyeSemantic&&p.system!=8&&p.system!=4&&p.system!=5,"existing instanced/stereo input requires a separate adapter");
        for(const auto& p:outputs)require(p.semantic!=eyeSemantic&&p.system!=4&&p.system!=5,"existing stereo/viewport output requires a separate adapter");
        const auto parsed=instructions(code);unsigned temps=0;uint32_t samplerSlots=0;bool foundTemps=false,body=false;std::map<unsigned,StereoConstantRange> bindings;
        for(const auto& inst:parsed){
            if(inst.opcode<88)body=true;else require(!body,"declarations after shader body");
            if(inst.opcode==dclTemps){require(inst.end-inst.start==2&&!foundTemps,"invalid temporary declaration");temps=code[inst.start+1];foundTemps=true;require(temps<4096,"temporary register limit");}
            if(inst.opcode==dclSampler){size_t at=inst.start+1;const auto o=operand(code,at,inst.end);
                require(at==inst.end&&o.type==sampler&&o.indices.size()==1&&o.reps[0]==0&&o.indices[0]<16,"unsupported sampler declaration");
                const auto bit=1u<<o.indices[0];require(!(samplerSlots&bit),"duplicate sampler declaration");samplerSlots|=bit;}
            if(inst.opcode==dclCB){size_t at=inst.start+1;const auto o=operand(code,at,inst.end);require(at==inst.end&&o.type==constant&&o.indices.size()==2&&o.reps[0]==0&&o.reps[1]==0,"unsupported CB declaration");
                const auto slot=o.indices[0],count=o.indices[1];require(slot<13&&count&&count<=4096,"packed constant slot unavailable or invalid CB size");
                if(slots&(1u<<slot)){require(!(code[inst.start]&(1u<<11)),"selected CB uses dynamic indexing");bindings.emplace(slot,StereoConstantRange{slot,count,0});}
            }
        }
        require(!bindings.empty(),"no selected CB declaration");StereoShaderVariant variant;variant.samplerSlots=samplerSlots;
        for(auto& item:bindings){item.second.offset=variant.eyeStride;variant.eyeStride+=item.second.registers;variant.constants.push_back(item.second);}
        require(variant.eyeStride<=2048,"paired constants exceed D3D11 CB capacity");
        const auto eyeInput=free_register(inputs);inputs.push_back({stage==StereoShaderStage::vertex?"SV_InstanceID":eyeSemantic,0,stage==StereoShaderStage::vertex?8u:0u,1,eyeInput,0x101});
        unsigned eyeOutput=0,arrayOutput=0;
        if(stage==StereoShaderStage::vertex){eyeOutput=free_register(outputs);outputs.push_back({eyeSemantic,0,0,1,eyeOutput,1});
            variant.geometrySource=geometry_source(outputs);if(direct){arrayOutput=free_register(outputs);outputs.push_back({"SV_RenderTargetArrayIndex",0,4,1,arrayOutput,1});}}
        Words converted{stage==StereoShaderStage::vertex?0x10050u:0x50u,0};bool inserted=false;
        auto declarations=[&]{
            if(!foundTemps)instruction(converted,dclTemps,{{1}});
            // A complete bank is dynamically indexed by the uniform eye value.
            instruction(converted,dclCB|(1u<<11),{{constant<<12|2u<<20,variant.packedSlot,variant.eyeStride*2}});
            if(stage==StereoShaderStage::vertex){instruction(converted,96,{reg(input,eyeInput,0,true),{8}});instruction(converted,101,{reg(output,eyeOutput,0,true)});if(direct)instruction(converted,103,{reg(output,arrayOutput,0,true),{4}});}
            else instruction(converted,98|(1u<<11),{reg(input,eyeInput,0,true)});
            instruction(converted,1,{reg(temporary,temps,0,true),reg(input,eyeInput,0,false),literal(1)});
            instruction(converted,38,{{13u<<12},reg(temporary,temps,1,true),reg(temporary,temps,0,false),literal(variant.eyeStride)});
        };
        for(const auto& inst:parsed){
            if(inst.opcode>=88){const auto start=converted.size();converted.insert(converted.end(),code.begin()+inst.start,code.begin()+inst.end);if(inst.opcode==dclTemps)converted[start+1]=temps+1;continue;}
            if(!inserted){declarations();inserted=true;}
            if(stage==StereoShaderStage::vertex&&(inst.opcode==ret||inst.opcode==retc)){
                instruction(converted,mov,{reg(output,eyeOutput,0,true),reg(temporary,temps,0,false)});if(direct)instruction(converted,mov,{reg(output,arrayOutput,0,true),reg(temporary,temps,0,false)});
            }
            const auto begin=converted.size();converted.push_back(code[inst.start]);size_t at=inst.start+1;
            auto extension=code[inst.start];while(extension>>31){require(at<inst.end,"truncated instruction extension");extension=code[at++];converted.push_back(extension);}
            while(at<inst.end){const auto o=operand(code,at,inst.end);bool rewritten=false;
                if(o.type==sampler)require(o.indices.size()==1&&o.reps[0]==0&&o.indices[0]<16&&(samplerSlots&(1u<<o.indices[0])),"undeclared or dynamically indexed sampler operand");
                if(o.type==constant){require(o.indices.size()==2&&o.reps[0]==0,"unsupported CB operand");const auto found=bindings.find(o.indices[0]);
                    if(found!=bindings.end()){require(o.reps[1]==0&&o.indices[1]<found->second.registers,"unsupported selected CB operand indexing");
                        converted.push_back((o.token&~(7u<<25))|(3u<<25));converted.insert(converted.end(),code.begin()+o.begin+1,code.begin()+o.indexStart);converted.push_back(variant.packedSlot);converted.push_back(found->second.offset+o.indices[1]);add(converted,reg(temporary,temps,1,false));rewritten=true;}
                }
                if(!rewritten)converted.insert(converted.end(),code.begin()+o.begin,code.begin()+o.end);
            }
            require(converted.size()-begin<128,"rewritten instruction too large");converted[begin]=(converted[begin]&0x80ffffffu)|(uint32_t(converted.size()-begin)<<24);
        }
        require(inserted,"empty shader body");converted[1]=uint32_t(converted.size());Bytes tokens(converted.size()*4);std::memcpy(tokens.data(),converted.data(),tokens.size());
        std::vector<Chunk> finalChunks{{ISGN,write_signature(inputs)},{OSGN,write_signature(outputs)},{SHEX,std::move(tokens)}};
        if(stage==StereoShaderStage::vertex&&direct){Bytes flags(8,0);store(flags,0,0x2000);finalChunks.push_back({fourcc('S','F','I','0'),std::move(flags)});}
        variant.bytecode=write_container(finalChunks);result=std::move(variant);return true;
    }catch(const std::exception& e){error=e.what();}catch(...){error="shader conversion allocation failed";}
    result={};return false;
}
}
