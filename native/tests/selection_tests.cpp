#include "notemdx/compiler.hpp"
#include <iostream>
#include <future>
#include <stdexcept>
using namespace notemdx;
int checks=0;
void check(bool b,const char* why) { ++checks; if(!b) throw std::runtime_error(why); }
Result select(std::string s,int a,int b,int line=1,int endline=-1) {
 Options o; o.selection=SelectionRange{line,a,endline<0?line:endline,b};
 auto r=compile({"test",s},o);
 if(!r.ok()) { for(auto&d:r.diagnostics) std::cerr<<d.message<<"\n"; }
 check(r.ok(),"selection compile failed"); return r;
}
std::vector<unsigned char> channel(const Result& r,int ch=0,int count=9) {
 size_t base=0;while(base+2<r.mdx.size() && !(r.mdx[base]==13 && r.mdx[base+1]==10 && r.mdx[base+2]==26)) ++base;
 base+=3;while(r.mdx.at(base))++base;++base;
 auto word=[&](size_t at){return size_t(r.mdx.at(at))*256+r.mdx.at(at+1);};
 size_t a=base+word(base+2+ch*2),b=base+word(base+(ch+1==count?0:4+ch*2));
 return {r.mdx.begin()+a,r.mdx.begin()+b};
}
int main() {
 auto r=select("A o5 l8 c r d e\n",13,16); // d e, prefix c/rest removed
 check(r.statistics.tracks[0].total_steps.value==48,"prefix duration survived");
 check(r.selected_channel=='A',"channel");
 r=select("B o4 c4\nA d1\n",1,5,1,2);
 check(r.selected_channel=='B',"source order must win over channel order");
 check(r.statistics.tracks[1].total_steps.value==48 && r.statistics.tracks[0].total_steps.value==192,"multi track selection");
 r=select("A [c4 d4]8 L e4\n",3,16);
 check(r.statistics.tracks[0].total_steps.value==816,"paired selection repeats");
 check(!r.statistics.tracks[0].loop_steps,"selection loop marker");
 r=select("A [c4 / d4]8 e4\n",4,6);
 check(r.statistics.tracks[0].total_steps.value==48,"partial repeat");
 r=select("A {c d e}4\n",6,7);
 check(r.statistics.tracks[0].total_steps.value==16,"partial tuplet retains original subdivision");
 r=select("I = \"c8 d8\"\nA l4 r4 $I e4\n",9,11,2);
 check(r.statistics.tracks[0].total_steps.value==48,"macro invocation after length command excludes initial rest");
 r=select("#compress 1\n#opt *\nA c4& c4 d4\n",7,10,3);
 check(r.statistics.tracks[0].total_steps.value==48,"selection compression boundary");
 r=select("A WA c4\n",1,8); // W removed; trailing A is a comment in this dialect
 // Test sync without a commented suffix separately.
 r=select("A W c4\n",1,8);
 check(r.statistics.tracks[0].total_steps.value==48,"sync waits must be removed");
 r=select("A c4 d4 e4\n",7,8);
 check(r.statistics.tracks[0].total_steps.value==0,"length-only selection must not trigger a note");
 auto normal=compile({"test","A [c4 d4]8 L e4\n"});
 check(normal.ok() && normal.statistics.tracks[0].total_steps.value==816,"normal repeat regression");
 check(normal.statistics.tracks[0].loop_steps->value==48,"normal loop regression");
 {
  auto s=std::string("A o5 l8 v10 p1 c r d e v2\n");
  auto first=int(s.find("d e"))+1;
  auto actual=select(s,first,first+3);
  auto expected=compile({"test","A o5 l8 v10 p1 d e\n"});
  check(channel(actual)==channel(expected),"prefix state or post-selection control leakage");
 }
 {
  Options o;o.selection=SelectionRange{2,3,2,5};
  o.include_loader=[](const std::string& name,const std::string&,Source& out,std::string&) {
   out={name,name=="before"?"A o5 l8 v10 c4\n":"A v2 d4\n"};return true;
  };
  auto actual=compile({"test","#include before\nA e8\n#include after\n"},o);
  auto expected=compile({"test","A o5 l8 v10 e8\n"});
  check(actual.ok() && channel(actual)==channel(expected),"include prefix/suffix isolation");
 }
 {
  Options o;o.selection=SelectionRange{1,3,1,8};
  auto ex=compile({"test","#ex-pcm\nP c4\n"},o);
  check(!ex.ok(),"directive-only selection should not play a track");
  o.selection=SelectionRange{2,3,2,5};
  ex=compile({"test","#ex-pcm\nQ c4\n"},o);
  check(ex.ok() && ex.selected_channel=='Q' && ex.statistics.tracks[9].total_steps.value==48,"extended PCM selection");
  check(channel(ex,0,16)==std::vector<unsigned char>({0xE8,0xF1,0}),"extended PCM header marker");
 }
 {
  auto actual=select("A t180 c1 t90 d1\nB c1 e4 f4\n",6,8,2);
  check(channel(actual,1)==channel(compile({"test","B t90 e4\n"}),1),"inherit A tempo at B selection time");
 }
 {
  auto actual=select("A c4_d4 e4\n",9,11);
  check(channel(actual)==channel(compile({"test","A e4\n"})),"prefix portamento must not leak");
 }
 {
  auto actual=select("A [c4 / d4]3 e4\n",3,13);
  check(actual.statistics.tracks[0].total_steps.value==240,"paired repeat final escape");
  actual=select("A [[c4]2 d4]3 e4\n",3,14);
  check(actual.statistics.tracks[0].total_steps.value==432,"nested paired repeats");
  actual=select("A [c4 [d4]3 e4]2\n",7,14);
  check(actual.statistics.tracks[0].total_steps.value==192,"partial outer complete inner repeat");
 }
 {
  auto actual=select("A t180 [c4 t90 d4]3 t120 e4\nB r%240 f4\n",9,11,2);
  check(channel(actual,1)==channel(compile({"test","B t90 r%240 f4\n"}),1),"inherit tempo through repeated A timing");
  actual=select("AB c4 d4\n",4,9);
  check(actual.statistics.tracks[0].total_steps.value==96 && actual.statistics.tracks[1].total_steps.value==96,"channel group selection");
 }
 {
  auto actual=select("A t180 [c4 t90 d4]3\n",8,19);
  auto expected=compile({"test","A t180 [c4 t90 d4]3\n"});
  check(channel(actual)==channel(expected),"inherited tempo must be outside repeat body");
 }
 {
  const Source source{"parallel", "#title parallel\nA t180 [c8d8]16 L e4\nB r4f4\n"};
  Options selected; selected.selection=SelectionRange{3,3,3,8};
  selected.editor_mapping=false;
  const auto full=compile(source), range=compile(source,selected);
  check(full.ok() && range.ok(), "parallel baselines");
  auto fullWorker=std::async(std::launch::async,[source,full] {
   for(int n=0;n<32;++n) {auto actual=compile(source);if(!actual.ok() || actual.mdx!=full.mdx)return false;}
   return true;
  });
  auto rangeWorker=std::async(std::launch::async,[source,range,selected] {
   for(int n=0;n<32;++n) {auto actual=compile(source,selected);
    if(!actual.ok() || actual.mdx!=range.mdx || actual.statistics.tracks[1].total_steps.value!=range.statistics.tracks[1].total_steps.value)return false;}
   return true;
  });
  check(fullWorker.get() && rangeWorker.get(),"concurrent full/selection compile changed results");
 }
 {
  auto actual=select("A r4 c4 d4\n",9,11);
  check(actual.statistics.tracks[0].total_steps.value==96,"initial rest not added to later selection");
  check(channel(actual)==channel(compile({"test","A r4 d4\n"})),"prefix note or rest order changed");
  actual=select("A c4 r4 d4\n",9,11);
  check(actual.statistics.tracks[0].total_steps.value==48,"non-initial rest added");
  actual=select("A r8 r12 c4 d4\n",12,14);
  check(actual.statistics.tracks[0].total_steps.value==88,"consecutive initial rests not summed");
  actual=select("A r4 c4\n",3,8);
  check(actual.statistics.tracks[0].total_steps.value==96,"selected initial rest duplicated");
  actual=select("A r4 r4 c4\n",6,11);
  check(actual.statistics.tracks[0].total_steps.value==144,"partially selected initial rests duplicated or omitted");
  actual=select("A [r8]3 c4 d4\n",12,14);
  check(actual.statistics.tracks[0].total_steps.value==48,"repeat command must not qualify as a leading rest");
  actual=select("I = \"r8 r12\"\nA $I c4 d4\n",9,11,2);
  check(actual.statistics.tracks[0].total_steps.value==48,"macro command must not qualify as a leading rest");
  actual=select("A {r r c}4 d4\n",12,14);
  check(actual.statistics.tracks[0].total_steps.value==48,"tuplet command must not qualify as a leading rest");
  actual=select("A l8 r8 c4 d4\n",12,14);
  check(actual.statistics.tracks[0].total_steps.value==48,"length command before rest must disable delay");
  actual=select("A v8 r8 c4 d4\n",12,14);
  check(actual.statistics.tracks[0].total_steps.value==48,"volume command before rest must disable delay");
  actual=select("A r8 v8 c4 d4\n",12,14);
  check(actual.statistics.tracks[0].total_steps.value==72,"rest before volume command must retain delay");
  actual=select("A r4 v8\n",6,8);
  check(actual.statistics.tracks[0].total_steps.value==0,"control-only selection gained initial delay");
 }
 std::cout<<checks<<" selection assertions passed\n";
}
