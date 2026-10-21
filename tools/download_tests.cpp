#include "service_net.h"
#include <cstdio>
#include <stdexcept>
#include <iostream>
int main(int argc,char** argv){
    if(argc!=3)return 2;
    try{
        services::initNetwork();uint64_t last=0;int intermediate=0;
        services::downloadFile(argv[1],argv[2],[&](uint64_t done,uint64_t total){
            if(done && done<total)intermediate++;
            last=done;
        });
        services::shutdownNetwork();
        if(last!=2*1024*1024 || intermediate<2)throw std::runtime_error("Streaming byte progress missing");
        std::cout<<"PASS: real streaming download with intermediate byte progress\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
