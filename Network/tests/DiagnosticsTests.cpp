#include "openrune/net/Diagnostics.h"
#include "openrune/net/Transport.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace openrune::net;
void check(bool v){if(!v)throw std::runtime_error("diagnostics regression");}
int main()
{
    TimingWindow frames;check(frames.summary(0).count==0);
    for(uint64_t i=0;i<100;++i)check(frames.add(i*10,i==99?200:10));
    auto s=frames.summary(1000);check(s.count==100 && s.mean==11.9 && s.p95==10 && s.maximum==200);
    check(!frames.add(989,10));check(!frames.add(1000,-1));
    check(!frames.add(1000,std::numeric_limits<double>::infinity()));
    check(!frames.add(1000,std::numeric_limits<double>::quiet_NaN()));
    check(frames.summary(500).count==0);check(frames.summary(1000).count==100);
    check(frames.summary(4000).count==0);frames.reset();check(frames.add(0,1));
    for(size_t i=0;i<TimingWindow::Capacity*3;++i)check(frames.add(1,double(i)));
    check(frames.summary(1).count==TimingWindow::Capacity);
    Transport tx(1,1),rx(1,1);check(tx.stats().rttSamples==0);
    check(tx.enqueue(Lane::Control,{42},0));auto first=tx.poll(0);check(first.has_value());
    check(rx.receiveTrusted(*first,10)==Receive::Accepted);
    auto ack=rx.poll(30);check(ack.has_value());check(tx.receiveTrusted(*ack,40)==Receive::Accepted);
    check(tx.stats().rttSamples==1 && tx.stats().dataSent==1 && tx.stats().acknowledged==1);
    check(tx.stats().observedRttMs==40);
    check(tx.stats().sentBytes==first->size() && rx.stats().receivedBytes==first->size());
    check(rx.receiveTrusted(*first,50)==Receive::Duplicate);
    check(rx.stats().duplicates==1 && rx.stats().receivedBytes==first->size()*2);
    check(tx.enqueue(Lane::Control,{43},50));auto second=tx.poll(50);check(second.has_value());
    check(rx.receiveTrusted(*second,60)==Receive::Accepted);ack=rx.poll(70);check(ack.has_value());
    check(tx.receiveTrusted(*ack,150)==Receive::Accepted);
    check(tx.stats().rttSamples==2 && tx.stats().rttVariationMs>0);
    check(tx.enqueue(Lane::Control,{44},151));check(tx.poll(151).has_value());
    check(tx.update(5000));check(tx.stats().losses==1 && tx.stats().dataSent==3);
    std::cout<<"PASS bounded timings, unavailable RTT, authenticated ACK samples, jitter and timeout counters\n";
}
