#include "HandHistory.h"
#include <iostream>
#include <fstream>
#include <sstream>
using namespace HandHistory;
int main(){
  std::ifstream f("two.txt"); std::stringstream ss; ss<<f.rdbuf();
  std::vector<Hand> hands; std::string err;
  if(!ParseAll(ss.str(),&hands,&err)){std::cout<<"ERR "<<err<<"\n";return 1;}
  std::cout<<"hands="<<hands.size()<<"\n";
  for(auto&h:hands){
    std::cout<<" #"<<h.hand_id<<" hero="<<h.default_hero<<" events="<<h.events.size()<<" decisions at:";
    for(size_t i=0;i<h.events.size();++i) if(IsDecision(h.events[i],h.default_hero)) std::cout<<" "<<i;
    std::cout<<"\n";
  }
  // final state hand 2
  State s=Replay(hands[1],"p4",hands[1].events.size());
  std::cout<<"hand2 end: p4 stack="<<s.players["p4"].stack<<" pot="<<s.pot<<"\n";
  State m=Replay(hands[1],"p4",9); // before flop? print board/bets
  std::cout<<"hand2 step9 board="<<m.board.size()<<" pot="<<m.pot<<"\n";
  // garbage in
  std::vector<Hand> g; std::string e2;
  std::cout<<"garbage ok? "<<ParseAll("hello world",&g,&e2)<<" err="<<e2.substr(0,30)<<"\n";
}
