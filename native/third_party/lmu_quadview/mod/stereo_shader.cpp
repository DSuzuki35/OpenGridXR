#include "stereo_shader.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace lmuqv {namespace {
using Words=std::vector<uint32_t>;using Bytes=std::vector<uint8_t>;
constexpr uint32_t input=1,output=2,temporary=0,immediate=4,sampler=6,constant=8;
constexpr uint32_t mov=54,ret=62,retc=63,dclCB=89,dclSampler=90,dclTemps=104;
constexpr char eyeSemantic[]="LMU_STEREO_EYE";
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
uint32_t word(const Bytes& data,size_t offset){require(offset<=data.size()&&data.size()-offset>=4,"truncated container");uint32_t v;std::memcpy(&v,data.data()+offset,4);return v;}
void append(Bytes& data,uint32_t value){const auto n=data.size();data.resize(n+4);std::memcpy(data.data()+n,&value,4);}
void store(Bytes& data,size_t offset,uint32_t value){require(offset<=data.size()&&data.size()-offset>=4,"invalid container offset");std::memcpy(data.data()+offset,&value,4);}
uint32_t rotate(uint32_t x,unsigned bits){return (x<<bits)|(x>>(32-bits));}
// DXBC uses MD5 compression with its container-specific length/padding layout.
// Independent implementation of the public DirectX token/container format and
// hash algorithm documented by Microsoft's DirectXShaderCompiler source:
// include/dxc/Support/d3d12TokenizedProgramFormat.hpp; lib/DxilHash/DxilHash.cpp.
// This checksum detects container damage; shader identity uses SHA256 elsewhere.
std::array<uint32_t,4> checksum(const Bytes& data){
    require(data.size()>=20&&data.size()<16*1024*1024,"invalid container length");
    const auto count=data.size()-20,full=count/64,tail=count%64;
    Bytes message(data.begin()+20,data.begin()+20+full*64);
    if(tail>=56){message.insert(message.end(),data.end()-tail,data.end());message.push_back(0x80);message.resize((full+1)*64,0);}
    const auto last=message.size();message.resize(last+64,0);store(message,last,uint32_t(count*8));
    if(tail<56){std::copy(data.end()-tail,data.end(),message.begin()+last+4);message[last+4+tail]=0x80;}
    store(message,last+60,uint32_t(count*2)|1);
    std::array<uint32_t,4> state{0x67452301,0xefcdab89,0x98badcfe,0x10325476};
    static const auto constants=[](){std::array<uint32_t,64> k{};for(unsigned i=0;i<64;++i)k[i]=uint32_t(std::fabs(std::sin(double(i+1)))*4294967296.0);return k;}();
    constexpr unsigned shifts[4][4]={{7,12,17,22},{5,9,14,20},{4,11,16,23},{6,10,15,21}};
    for(size_t offset=0;offset<message.size();offset+=64){
        auto a=state[0],b=state[1],c=state[2],d=state[3];
        for(unsigned i=0;i<64;++i){uint32_t f=0,index=0;
            if(i<16){f=(b&c)|(~b&d);index=i;}else if(i<32){f=(d&b)|(~d&c);index=(5*i+1)%16;}
            else if(i<48){f=b^c^d;index=(3*i+5)%16;}else{f=c^(b|~d);index=(7*i)%16;}
            const auto next=b+rotate(a+f+constants[i]+word(message,offset+4*index),shifts[i/16][i%4]);a=d;d=c;c=b;b=next;
        }state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;
    }return state;
}
struct Chunk {uint32_t type=0;Bytes data;};
constexpr uint32_t fourcc(char a,char b,char c,char d){return uint32_t(a)|(uint32_t(b)<<8)|(uint32_t(c)<<16)|(uint32_t(d)<<24);}
constexpr auto ISGN=fourcc('I','S','G','N'),OSGN=fourcc('O','S','G','N'),SHDR=fourcc('S','H','D','R'),SHEX=fourcc('S','H','E','X');
std::vector<Chunk> read_container(const void* raw,size_t size){
    require(raw&&size>=32&&size<16*1024*1024,"invalid shader data");const auto* p=static_cast<const uint8_t*>(raw);Bytes data(p,p+size);
    require(word(data,0)==fourcc('D','X','B','C')&&word(data,20)==1&&word(data,24)==size,"invalid DXBC header");
    const auto digest=checksum(data);require(!std::memcmp(data.data()+4,digest.data(),16),"invalid DXBC checksum");
    const auto count=word(data,28);require(count<=64&&32+size_t(count)*4<=size,"invalid DXBC chunk count");
    std::vector<Chunk> chunks;std::set<uint32_t> unique;std::vector<std::pair<size_t,size_t>> ranges;
    for(unsigned i=0;i<count;++i){const auto offset=word(data,32+4*i);const auto type=word(data,offset),n=word(data,size_t(offset)+4);
        require(offset>=32+count*4&&(offset&3)==0&&size_t(offset)+8+n<=size,"invalid DXBC chunk range");require(unique.insert(type).second,"duplicate DXBC chunk");
        for(const auto& range:ranges)require(size_t(offset)+8+n<=range.first||offset>=range.second,"overlapping DXBC chunks");ranges.push_back({offset,size_t(offset)+8+n});
        chunks.push_back({type,Bytes(data.begin()+offset+8,data.begin()+offset+8+n)});
    }return chunks;
}
Bytes write_container(const std::vector<Chunk>& chunks){
    Bytes result(32+4*chunks.size(),0);store(result,0,fourcc('D','X','B','C'));store(result,20,1);store(result,28,uint32_t(chunks.size()));
    for(size_t i=0;i<chunks.size();++i){while(result.size()%4)result.push_back(0);store(result,32+4*i,uint32_t(result.size()));append(result,chunks[i].type);append(result,uint32_t(chunks[i].data.size()));result.insert(result.end(),chunks[i].data.begin(),chunks[i].data.end());}
    store(result,24,uint32_t(result.size()));const auto digest=checksum(result);std::memcpy(result.data()+4,digest.data(),16);return result;
}
struct Parameter {std::string semantic;uint32_t index=0,system=0,type=0,reg=0,masks=0;};
std::vector<Parameter> read_signature(const Bytes& data){
    const auto count=word(data,0);require(count<=32&&word(data,4)==8&&8+size_t(count)*24<=data.size(),"unsupported signature format");std::vector<Parameter> result;
    for(unsigned i=0;i<count;++i){const size_t offset=8+24*i;const auto name=word(data,offset);require(name>=8+count*24&&name<data.size(),"invalid signature name");
        const auto end=std::find(data.begin()+name,data.end(),uint8_t(0));require(end!=data.end()&&end-(data.begin()+name)<=128,"invalid signature semantic");
        Parameter p;p.semantic=std::string(data.begin()+name,end);p.index=word(data,offset+4);p.system=word(data,offset+8);p.type=word(data,offset+12);p.reg=word(data,offset+16);p.masks=word(data,offset+20);
        require(!p.semantic.empty()&&p.semantic.size()<=64&&p.index<=32,"unsupported signature semantic");
        for(char c:p.semantic)require((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_',"unsupported signature identifier");
        require(p.reg<32&&p.type>=1&&p.type<=3&&(p.masks&15),"unsupported signature parameter");result.push_back(std::move(p));
    }return result;
}
Bytes write_signature(const std::vector<Parameter>& params){
    Bytes result(8+24*params.size(),0);store(result,0,uint32_t(params.size()));store(result,4,8);
    for(size_t i=0;i<params.size();++i){const auto& p=params[i];const auto offset=8+24*i;store(result,offset,uint32_t(result.size()));result.insert(result.end(),p.semantic.begin(),p.semantic.end());result.push_back(0);
        store(result,offset+4,p.index);store(result,offset+8,p.system);store(result,offset+12,p.type);store(result,offset+16,p.reg);store(result,offset+20,p.masks);
    }while(result.size()%4)result.push_back(0);return result;
}
unsigned free_register(const std::vector<Parameter>& params){unsigned n=0;for(const auto& p:params)n=(std::max)(n,p.reg+1);require(n<32,"no free signature register");return n;}
struct Operand {uint32_t token=0,type=0;size_t begin=0,indexStart=0,end=0;std::vector<uint32_t> indices,reps;};
Operand operand(const Words& code,size_t& at,size_t end,unsigned depth=0){
    require(depth<8&&at<end,"truncated operand");Operand o;o.begin=at;o.token=code[at++];o.type=(o.token>>12)&255;
    const auto components=o.token&3;require(components!=3,"unsupported operand component count");
    uint32_t extension=o.token;while(extension>>31){require(at<end,"truncated operand extension");extension=code[at++];}
    o.indexStart=at;const auto dimensions=(o.token>>20)&3;
    for(unsigned i=0;i<dimensions;++i){const auto rep=(o.token>>(22+3*i))&7;require(rep==0||rep==2||rep==3,"unsupported operand indexing");o.reps.push_back(rep);
        uint32_t index=0;if(rep==0||rep==3){require(at<end,"truncated operand index");index=code[at++];}o.indices.push_back(index);
        if(rep==2||rep==3)operand(code,at,end,depth+1);
    }
    if(o.type==4||o.type==5){const auto values=(components==2?4u:components==1?1u:0u)*(o.type==5?2u:1u);require(at+values<=end,"truncated immediate operand");at+=values;}
    o.end=at;return o;
}
Words reg(uint32_t type,unsigned index,unsigned component,bool destination){return {2u|(destination?(1u<<(4+component)):(8u|(component<<4)))|(type<<12)|(1u<<20),index};}
Words literal(uint32_t value){return {1u|(immediate<<12),value};}
void add(Words& out,const Words& tokens){out.insert(out.end(),tokens.begin(),tokens.end());}
void instruction(Words& out,uint32_t opcode,std::initializer_list<Words> operands){const auto start=out.size();out.push_back(opcode);for(const auto& o:operands)add(out,o);require(out.size()-start<128,"instruction too large");out[start]|=uint32_t(out.size()-start)<<24;}
struct Instruction {size_t start=0,end=0;unsigned opcode=0;};
std::vector<Instruction> instructions(const Words& code){
    require(code.size()>=2&&code[1]==code.size(),"invalid token count");std::vector<Instruction> result;
    for(size_t start=2;start<code.size();){const auto op=code[start]&2047,length=(code[start]>>24)&127;
        require(op<=106&&op!=53&&length&&start+length<=code.size(),"unsupported or malformed shader instruction");
        // The first converter deliberately excludes function calls, geometry
        // emission, indexable temporaries and SM5-specific instructions.
        require(op!=4&&op!=5&&op!=9&&op!=19&&op!=20&&op!=44&&op!=91&&!(op>=92&&op<=94)&&op!=105,"unsupported shader control/declaration");
        result.push_back({start,start+length,op});start+=length;
    }return result;
}
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
