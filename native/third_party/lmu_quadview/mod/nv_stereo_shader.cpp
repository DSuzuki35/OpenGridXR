#include "nv_stereo_shader.hpp"
#include "dxbc_shader_internal.hpp"

namespace lmuqv {namespace {
std::string family(const Parameter& p){
    std::string name="LMU_ATTR_";for(char c:p.semantic)name+=c>='a'&&c<='z'?char(c-'a'+'A'):c;
    return name+"_"+std::to_string(p.index);
}
std::string view_semantic(const std::string& base,unsigned view){return base+"_VIEW_"+std::to_string(view)+"_SEMANTIC";}
void custom(NvStereoShaderVariant& v,uint32_t type,const std::string& name){
    for(const auto& item:v.customSemantics)if(item.name==name){require(item.type==type,"conflicting custom semantic");return;}
    require(v.customSemantics.size()<32,"custom semantic limit");v.customSemantics.push_back({type,name});
}
}
bool convert_nv_stereo_shader(const void* raw,size_t size,StereoShaderStage stage,uint32_t slots,unsigned views,NvStereoShaderVariant& result,std::string& error){
    result={};error.clear();try{
        require(views==2||views==4,"NV adapter requires two or four views");require(!(slots&~0x1fffu),"eye-dependent CB selection must use slots 0-12");
        const bool vertex=stage==StereoShaderStage::vertex;const auto chunks=read_container(raw,size);std::vector<Parameter> inputs,outputs;Words code;
        for(const auto& chunk:chunks){if(chunk.type==ISGN)inputs=read_signature(chunk.data);else if(chunk.type==OSGN)outputs=read_signature(chunk.data);
            else if(chunk.type==SHDR){require(chunk.data.size()%4==0,"unaligned shader tokens");code.resize(chunk.data.size()/4);std::memcpy(code.data(),chunk.data.data(),chunk.data.size());}}
        require(!inputs.empty()&&!outputs.empty()&&!code.empty(),"SM4 input/output signatures and SHDR required");require(code[0]==(vertex?0x10040u:0x40u),"NV adapter supports SM4.0 VS/PS only");
        for(const auto& p:inputs)require(p.semantic.rfind("LMU_",0)!=0&&p.semantic.rfind("NV_",0)!=0&&p.system!=4&&p.system!=5,"existing stereo/viewport input requires a separate adapter");
        for(const auto& p:outputs)require(p.semantic.rfind("LMU_",0)!=0&&p.semantic.rfind("NV_",0)!=0&&p.system!=4&&p.system!=5,"existing stereo/viewport output requires a separate adapter");
        const auto parsed=instructions(code);NvStereoShaderVariant variant;variant.views=views;custom(variant,5,"NV_POSITION");
        std::map<unsigned,StereoConstantRange> bindings;unsigned temps=0;bool foundTemps=false,body=false;size_t bodyStart=parsed.size();
        for(size_t i=0;i<parsed.size();++i){const auto& inst=parsed[i];if(inst.opcode<88){if(!body)bodyStart=i;body=true;}else require(!body,"declarations after shader body");
            if(inst.opcode==dclTemps){require(inst.end-inst.start==2&&!foundTemps,"invalid temporary declaration");temps=code[inst.start+1];foundTemps=true;require(temps<4096,"temporary register limit");}
            if(inst.opcode==dclSampler){size_t at=inst.start+1;const auto o=operand(code,at,inst.end);require(at==inst.end&&o.type==sampler&&o.indices.size()==1&&o.reps[0]==0&&o.indices[0]<16,"unsupported sampler declaration");require(!(variant.samplerSlots&(1u<<o.indices[0])),"duplicate sampler declaration");variant.samplerSlots|=1u<<o.indices[0];}
            if(inst.opcode==dclCB){size_t at=inst.start+1;const auto o=operand(code,at,inst.end);require(at==inst.end&&o.type==constant&&o.indices.size()==2&&o.reps[0]==0&&o.reps[1]==0,"unsupported CB declaration");
                const auto slot=o.indices[0],count=o.indices[1];require(slot<13&&count&&count<=4096,"packed constant slot unavailable or invalid CB size");
                if(slots&(1u<<slot)){require(!(code[inst.start]&(1u<<11)),"selected CB uses dynamic indexing");require(bindings.emplace(slot,StereoConstantRange{slot,count,0}).second,"duplicate selected CB declaration");}}
            if(vertex&&(inst.opcode==ret||inst.opcode==retc))require(inst.opcode==ret&&i+1==parsed.size(),"NV vertex adapter requires one terminal return");
        }
        require(body&&parsed.back().opcode==ret,"shader requires a terminal return");
        for(auto& item:bindings){item.second.offset=variant.eyeStride;variant.eyeStride+=item.second.registers;variant.constants.push_back(item.second);}
        require(variant.eyeStride<=4096/views,"multiview constants exceed D3D11 CB capacity");
        std::map<unsigned,unsigned> outputBase;std::set<unsigned> expandedInputs;std::vector<Parameter> convertedOutputs;
        if(vertex){
            require((outputs.size()+1)*views+2<=32,"multiview outputs exceed D3D11 signature capacity");unsigned positionCount=0;
            for(const auto& p:outputs){require(p.system==0||p.system==1,"unsupported multiview output system semantic");require(outputBase.emplace(p.reg,unsigned(convertedOutputs.size())).second,"shared output registers require a separate adapter");
                if(p.system==1){require(p.type==3&&(p.masks&15)==15,"position must be float4");++positionCount;}else custom(variant,10,family(p));
                for(unsigned view=0;view<views;++view){auto q=p;q.reg=unsigned(convertedOutputs.size());if(p.system==1){if(view){q.semantic=view_semantic("NV_POSITION",view);q.index=0;q.system=0;}}
                    else {q.semantic=view_semantic(family(p),view);q.index=0;q.masks=15;}convertedOutputs.push_back(std::move(q));}}
            require(positionCount==1,"multiview requires one position output");
            for(unsigned view=0;view<views;++view)convertedOutputs.push_back({view_semantic(eyeSemantic,view),0,0,1,unsigned(convertedOutputs.size()),15});
            convertedOutputs.push_back({"NV_VIEWPORT_MASK",0,0,1,unsigned(convertedOutputs.size()),15});
            convertedOutputs.push_back({"NV_VIEWPORT_MASK_2_SEMANTIC",0,0,1,unsigned(convertedOutputs.size()),15});
        }else{
            std::set<unsigned> registers;convertedOutputs=outputs;for(auto& p:inputs){require(registers.insert(p.reg).second,"shared pixel input registers require a separate adapter");require(p.system==0||p.system==1||p.system==7,"unsupported multiview pixel system input");if(!p.system){custom(variant,10,family(p));expandedInputs.insert(p.reg);p.semantic=view_semantic(family(p),0);p.index=0;p.masks=(p.masks&~15u)|15u;}}
        }
        custom(variant,2,"NV_VIEWPORT_MASK");custom(variant,4,"NV_VIEWPORT_MASK_2_SEMANTIC");
        custom(variant,10,eyeSemantic);const auto eyeInput=vertex?0:free_register(inputs);if(!vertex)inputs.push_back({view_semantic(eyeSemantic,0),0,0,1,eyeInput,0x10f});
        Words converted{vertex?0x10050u:0x50u,0};
        // Copy native declarations except output declarations, which are rebuilt
        // after register cloning. Original input/instance IDs remain untouched.
        for(size_t i=0;i<bodyStart;++i){const auto& inst=parsed[i];if(vertex&&inst.opcode>=101&&inst.opcode<=103)continue;
            const auto start=converted.size();converted.insert(converted.end(),code.begin()+inst.start,code.begin()+inst.end);if(!vertex&&inst.opcode==dclTemps)converted[start+1]=temps+1;
            if(!vertex&&inst.opcode>=98&&inst.opcode<=100){size_t at=inst.start+1;const auto o=operand(code,at,inst.end);require(o.type==input&&o.indices.size()==1&&o.reps[0]==0,"unsupported pixel input declaration");if(expandedInputs.count(o.indices[0]))converted[start+o.begin-inst.start]=(o.token&~0xf0u)|0xf0u;}}
        if(!vertex&&!foundTemps)instruction(converted,dclTemps,{{1}});
        if(variant.eyeStride)instruction(converted,dclCB|(!vertex?1u<<11:0u),{{constant<<12|2u<<20,variant.packedSlot,variant.eyeStride*views}});
        if(vertex){for(const auto& p:convertedOutputs){Words o{2u|((p.masks&15)<<4)|(output<<12)|(1u<<20),p.reg};if(p.system)instruction(converted,103,{o,{p.system}});else instruction(converted,101,{o});}}
        else {instruction(converted,98|(1u<<11),{{2u|(15u<<4)|(input<<12)|(1u<<20),eyeInput}});instruction(converted,38,{{13u<<12},reg(temporary,temps,1,true),reg(input,eyeInput,0,false),literal(variant.eyeStride)});}
        for(unsigned view=0;view<(vertex?views:1u);++view){
            // NV generic groups require a full four-component vector. Initialize
            // channels outside the native output mask before replaying its writes.
            if(vertex)for(const auto& p:outputs)if(!p.system)instruction(converted,mov,{{2u|(15u<<4)|(output<<12)|(1u<<20),outputBase.at(p.reg)+view},literal(0)});
            for(size_t i=bodyStart;i<parsed.size();++i){const auto& inst=parsed[i];if(vertex&&inst.opcode==ret)continue;
                const auto begin=converted.size();converted.push_back(code[inst.start]);size_t at=inst.start+1;auto extension=code[inst.start];while(extension>>31){require(at<inst.end,"truncated instruction extension");extension=code[at++];converted.push_back(extension);}
                while(at<inst.end){const auto o=operand(code,at,inst.end);bool rewritten=false;
                    if(o.type==sampler)require(o.indices.size()==1&&o.reps[0]==0&&o.indices[0]<16&&(variant.samplerSlots&(1u<<o.indices[0])),"undeclared or dynamically indexed sampler operand");
                    if(vertex&&o.type==output){require(o.indices.size()==1&&o.reps[0]==0,"unsupported output indexing");const auto found=outputBase.find(o.indices[0]);require(found!=outputBase.end(),"output absent from signature");converted.insert(converted.end(),code.begin()+o.begin,code.begin()+o.indexStart);converted.push_back(found->second+view);rewritten=true;}
                    if(o.type==constant){require(o.indices.size()==2&&o.reps[0]==0,"unsupported CB operand");const auto found=bindings.find(o.indices[0]);
                        if(found!=bindings.end()){require(o.reps[1]==0&&o.indices[1]<found->second.registers,"unsupported selected CB operand indexing");converted.push_back(vertex?o.token:(o.token&~(7u<<25))|(3u<<25));converted.insert(converted.end(),code.begin()+o.begin+1,code.begin()+o.indexStart);converted.push_back(variant.packedSlot);converted.push_back(found->second.offset+o.indices[1]+(vertex?view*variant.eyeStride:0));if(!vertex)add(converted,reg(temporary,temps,1,false));rewritten=true;}}
                    if(!rewritten)converted.insert(converted.end(),code.begin()+o.begin,code.begin()+o.end);
                }
                require(converted.size()-begin<128,"rewritten instruction too large");converted[begin]=(converted[begin]&0x80ffffffu)|(uint32_t(converted.size()-begin)<<24);
            }
            if(vertex)instruction(converted,mov,{{2u|(15u<<4)|(output<<12)|(1u<<20),unsigned(outputs.size())*views+view},literal(view)});
        }
        if(vertex){
            // Array-target mode: each active view uses viewport zero. The four
            // uint components encode the per-view masks (PG-09189-001_v01 §2.3.1).
            const unsigned mask=unsigned(convertedOutputs.size())-2;
            instruction(converted,mov,{{2u|(15u<<4)|(output<<12)|(1u<<20),mask},literal(1)});
            instruction(converted,mov,{{2u|(15u<<4)|(output<<12)|(1u<<20),mask+1},literal(0)});
            instruction(converted,ret,{});
        }converted[1]=uint32_t(converted.size());Bytes tokens(converted.size()*4);std::memcpy(tokens.data(),converted.data(),tokens.size());
        variant.bytecode=write_container({{ISGN,write_signature(inputs)},{OSGN,write_signature(convertedOutputs)},{SHEX,std::move(tokens)}});result=std::move(variant);return true;
    }catch(const std::exception& e){error=e.what();}catch(...){error="NV shader conversion allocation failed";}result={};return false;
}
}
