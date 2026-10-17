#include "gamePatches.hpp"
#include "dashboardPaths.hpp"
#include "3rdparty/json.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>

namespace {
// SHA-256 for exact executable-version matching. No downloaded code is executed.
class Sha256 {
  uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  unsigned char block[64]={}; size_t used=0; uint64_t length=0;
  static uint32_t r(uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
  void compress(){
    static const uint32_t k[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t w[64];for(unsigned i=0;i<16;i++)w[i]=(uint32_t(block[i*4])<<24)|(uint32_t(block[i*4+1])<<16)|(uint32_t(block[i*4+2])<<8)|block[i*4+3];
    for(unsigned i=16;i<64;i++)w[i]=w[i-16]+(r(w[i-15],7)^r(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(r(w[i-2],17)^r(w[i-2],19)^(w[i-2]>>10));
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],v=h[7];
    for(unsigned i=0;i<64;i++){uint32_t t=v+(r(e,6)^r(e,11)^r(e,25))+((e&f)^(~e&g))+k[i]+w[i],u=(r(a,2)^r(a,13)^r(a,22))+((a&b)^(a&c)^(b&c));v=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;}h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=v;
  }
public:
  void update(const unsigned char* bytes,size_t n){length+=n;while(n){size_t part=std::min(n,64-used);memcpy(block+used,bytes,part);used+=part;bytes+=part;n-=part;if(used==64){compress();used=0;}}}
  std::string finish(){uint64_t bits=length*8;unsigned char byte=0x80;update(&byte,1);byte=0;while(used!=56)update(&byte,1);unsigned char end[8];for(unsigned i=0;i<8;i++)end[7-i]=static_cast<unsigned char>(bits>>(i*8));update(end,8);char out[65]={};for(unsigned i=0;i<8;i++)snprintf(out+i*8,9,"%08x",h[i]);return out;}
};
bool digest(const std::string& path,std::string& hash){FILE* f=fopen(path.c_str(),"rb");if(!f)return false;Sha256 sha;std::array<unsigned char,65536> buf;size_t n;while((n=fread(buf.data(),1,buf.size(),f)))sha.update(buf.data(),n);bool good=!ferror(f);fclose(f);if(good)hash=sha.finish();return good;}
uint32_t le(const unsigned char* b){return uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);}
bool safePath(const std::string& p){if(p.empty()||p.size()>220||p.front()=='/'||p.back()=='/'||p.find(':')!=std::string::npos||p.find('\\')!=std::string::npos)return false;size_t start=0;while(start<p.size()){size_t end=p.find('/',start);std::string s=p.substr(start,end==std::string::npos?p.size()-start:end-start);if(s.empty()||s=="."||s=="..")return false;start=end==std::string::npos?p.size():end+1;}return true;}
std::string diskPath(std::string s){std::replace(s.begin(),s.end(),'/','\\');return s;}
bool goodName(const std::string& s){if(s.empty()||s.size()>42)return false;for(unsigned char c:s)if(c<32||c>126||strchr("\"*/:<>?\\|",c))return false;return true;}
bool stringField(const nlohmann::json& j,const char* key,std::string& out){if(!j.is_object()||!j.contains(key)||!j[key].is_string())return false;out=j[key].get<std::string>();return true;}
bool hex(const std::string& text,std::vector<unsigned char>& out){if(text.empty()||text.size()>8192||text.size()%2)return false;out.clear();for(size_t i=0;i<text.size();i+=2){auto digit=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};int a=digit(text[i]),b=digit(text[i+1]);if(a<0||b<0)return false;out.push_back(static_cast<unsigned char>((a<<4)|b));}return true;}
bool hashText(const std::string& s){if(s.size()!=64)return false;for(char c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
}

bool GamePatchPlan::prepare(const std::string& sourceRoot,std::string& error){
  renames.clear();files.clear();excluded.clear();label.clear();FILE* xbe=fopen((sourceRoot+"default.xbe").c_str(),"rb");if(!xbe){error="Cannot inspect the game executable.";return false;}
  unsigned char header[0x11c]={},certificate[12]={};bool ok=fread(header,1,sizeof(header),xbe)==sizeof(header)&&le(header)==0x48454258;
  uint32_t base=le(header+0x104),cert=le(header+0x118);ok=ok&&cert>=base&&cert-base<=0x7ffffff0&&fseek(xbe,cert-base,SEEK_SET)==0&&fread(certificate,1,12,xbe)==12;fclose(xbe);
  if(!ok){error="Invalid game XBE header.";return false;}char title[9]={};snprintf(title,sizeof(title),"%08x",le(certificate+8));
  std::ifstream input(dashboardDataPath((std::string("game-patches/")+title+".json").c_str()));if(!input)return true;
  std::string document((std::istreambuf_iterator<char>(input)),{});if(document.size()>262144){error="Patch profile is too large.";return false;}
  auto root=nlohmann::json::parse(document,nullptr,false);std::string profileTitle;
  if(root.is_discarded()||!root.is_object()||!root.contains("format")||!root["format"].is_number_integer()||root["format"].get<int>()!=1||!stringField(root,"title_id",profileTitle)||profileTitle!=title||!root.contains("versions")||!root["versions"].is_array()){error="Invalid compatibility profile.";return false;}
  std::string hash;if(!digest(sourceRoot+"default.xbe",hash)){error="Could not fingerprint the game version.";return false;}
  const nlohmann::json* selected=nullptr;for(const auto& version:root["versions"]){std::string expected;if(stringField(version,"default_xbe_sha256",expected)&&expected==hash){if(selected){error="Ambiguous patch profile.";return false;}selected=&version;}}
  if(!selected)return true;const auto& version=*selected;
  if(!stringField(version,"label",label)||label.size()>120||!version.contains("renames")||!version["renames"].is_array()||version["renames"].size()>128||!version.contains("files")||!version["files"].is_array()||version["files"].size()>8){error="Invalid patch rule fields.";return false;}
  for(const auto& j:version["renames"]){Rename rule;if(!stringField(j,"from_prefix",rule.from)||!stringField(j,"to_prefix",rule.to)||!safePath(rule.from)||!safePath(rule.to)||rule.from.substr(0,rule.from.find_last_of('/'))!=rule.to.substr(0,rule.to.find_last_of('/'))){error="Unsafe filename patch rule.";return false;}renames.push_back(rule);}
  std::set<std::string> targets;
  for(const auto& j:version["files"]){File file;if(!stringField(j,"path",file.path)||!safePath(file.path)||!targets.insert(file.path).second||!stringField(j,"sha256",file.hash)||!hashText(file.hash)||!j.contains("patches")||!j["patches"].is_array()||j["patches"].size()>128){error="Invalid binary patch target.";return false;}
    std::string actual;if(!digest(sourceRoot+diskPath(file.path),actual)||actual!=file.hash){error="Compatibility patch does not match "+file.path;return false;}
    FILE* original=fopen((sourceRoot+diskPath(file.path)).c_str(),"rb");if(!original){error="Cannot read patch target.";return false;}
    for(const auto& patch:j["patches"]){Edit edit;std::string before,after;if(!patch.is_object()||!patch.contains("offset")||!patch["offset"].is_number_unsigned()||patch["offset"].get<uint64_t>()>0x7ffffff0||!stringField(patch,"before",before)||!stringField(patch,"after",after)||!hex(before,edit.before)||!hex(after,edit.after)||edit.before.size()!=edit.after.size()){fclose(original);error="Invalid fixed-size binary patch.";return false;}
      edit.offset=patch["offset"].get<uint32_t>();for(const auto& existing:file.edits)if(uint64_t(edit.offset)<uint64_t(existing.offset)+existing.before.size()&&uint64_t(existing.offset)<uint64_t(edit.offset)+edit.before.size()){fclose(original);error="Overlapping binary patches.";return false;}
      std::vector<unsigned char> bytes(edit.before.size());if(fseek(original,edit.offset,SEEK_SET)!=0||fread(bytes.data(),1,bytes.size(),original)!=bytes.size()||bytes!=edit.before){fclose(original);error="Expected game bytes do not match.";return false;}file.edits.push_back(edit);
    }fclose(original);files.push_back(file);
  }
  if(version.contains("exclude_build_logs")) {
    const auto& list=version["exclude_build_logs"];
    if(!list.is_array()||list.size()>128){error="Invalid build-log exclusion list.";return false;}
    for(const auto& item:list){
      std::string path,expected,actual;
      if(!stringField(item,"path",path)||!safePath(path)||path.compare(0,8,"Effects/")!=0||path.size()<4||path.substr(path.size()-4)!=".txt"||!stringField(item,"sha256",expected)||!hashText(expected)||!digest(sourceRoot+diskPath(path),actual)||actual!=expected){error="Asset-build log fingerprint does not match.";return false;}
      excluded.push_back(path);
    }
  }
  if(!renames.empty()&&files.empty()){error="Filename changes have no matching executable patches.";return false;}return true;
}
bool GamePatchPlan::skipFile(const std::string& relative)const{std::string path=relative;std::replace(path.begin(),path.end(),'\\','/');return std::find(excluded.begin(),excluded.end(),path)!=excluded.end();}
std::string GamePatchPlan::mapPath(std::string relative)const{std::replace(relative.begin(),relative.end(),'\\','/');for(const auto& rename:renames)if(relative.compare(0,rename.from.size(),rename.from)==0)return rename.to+relative.substr(rename.from.size());return relative;}
std::string GamePatchPlan::destinationName(const std::string& relative)const{auto path=mapPath(relative);auto slash=path.find_last_of('/');return slash==std::string::npos?path:path.substr(slash+1);}
bool GamePatchPlan::validateName(const std::string& relative,std::string& error)const{auto path=mapPath(relative);if(!safePath(path)||!goodName(destinationName(relative))){error="FATX-incompatible name: "+relative+". No verified patch for this game version; copy not started.";return false;}return true;}
bool GamePatchPlan::apply(const std::string& destinationRoot,std::string& error)const{
  for(const auto& file:files){std::string path=destinationRoot+diskPath(mapPath(file.path)),actual;if(!digest(path,actual)||actual!=file.hash){error="Copied executable did not match its patch fingerprint: "+file.path;return false;}}
  for(const auto& file:files){FILE* output=fopen((destinationRoot+diskPath(mapPath(file.path))).c_str(),"r+b");if(!output){error="Cannot open copied game for patching.";return false;}
    for(const auto& edit:file.edits){std::vector<unsigned char> bytes(edit.before.size());if(fseek(output,edit.offset,SEEK_SET)!=0||fread(bytes.data(),1,bytes.size(),output)!=bytes.size()||bytes!=edit.before||fseek(output,edit.offset,SEEK_SET)!=0||fwrite(edit.after.data(),1,edit.after.size(),output)!=edit.after.size()){fclose(output);error="Game patch failed; incomplete copy will be removed.";return false;}}
    const bool flushed = fflush(output)==0; const bool closed = fclose(output)==0; if(!flushed||!closed){error="Could not save the compatibility patch.";return false;}
  }return true;
}
std::string GamePatchPlan::description()const{return files.empty()&&excluded.empty()?"":"Compatibility profile applied: "+label;}
