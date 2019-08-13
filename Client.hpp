

#ifndef __M_CLI_H__
#define __M_CLI_H__

#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <thread>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include <boost/algorithm/string.hpp> //字符串切割
#include "httplib.h"

#define RANGE_SIZE (100<<20)    //分块大小,100左移20位就是100M

using namespace httplib;
namespace bf = boost::filesystem;

class P2PClient{
  private:
    uint16_t srv_port;
    int host_index;     //用户选择的ip地址
    std::vector<std::string> online_list; //在线主机列表
    std::vector<std::string> file_list;   //文件列表

  private:

    //1. 获取局域网中所有ip地址
    bool GetAllHost(std::vector<std::string>& list){  
      struct ifaddrs* addrs = NULL;
      struct sockaddr_in* ip = NULL;
      struct sockaddr_in* mask = NULL;

      getifaddrs(&addrs);

      for(;addrs != NULL;addrs = addrs->ifa_next){
        ip = (struct sockaddr_in*)addrs->ifa_addr;
        mask = (struct sockaddr_in*) addrs->ifa_netmask;
        if(ip->sin_family != AF_INET){
          continue;
        }
        if(ip->sin_addr.s_addr == inet_addr("127.0.0.1")){
          continue;
        }

        uint32_t net,host;    //网络号与最大主机号
        net = ntohl(ip->sin_addr.s_addr & mask->sin_addr.s_addr);
        host = ntohl(~mask->sin_addr.s_addr);
        for(unsigned i = 2;i < host - 1;++i){     //1 ~ 253略过1和254这两个处理很慢的
          struct in_addr ip;
          ip.s_addr = htonl(net + i); //主机字节序 -> 网络字节序
          list.push_back(inet_ntoa(ip));
        }
      }
      freeifaddrs(addrs);   //内部动态申请了资源,要及时释放
      return true;
    }

    //创建客户端,发起请求
    void HostPair(std::string& i){  
      Client client(i.c_str(),srv_port);  //i[0]：字符串
      //auto rsp = client.Get("/hostpair");   //获取响应
      //if(rsp && rsp->status == 200){
      auto req = client.Get("/hostpair");   //获取响应
      if(req && req->status == 200){
        std::cerr << "Host " << i << "pair success\n";
        online_list.push_back(i);
      }
      std::cerr << "Host " << i << "pair failed\n";
      //printf("%s\n",i.c_str());
      return;
    }

    //2. 获取在线列表配对,向对方发送请求
    bool GetOnlineHost(std::vector<std::string>& list){ 
      //每次执行都刷新一下列表
      online_list.clear();
      std::vector<std::thread> thr_list(list.size());  //线程列表
      for(int i = 0;i < (int)list.size();++i){
        std::thread thr(&P2PClient::HostPair,this,std::ref(list[i]));   //实例化一个线程对象
        //HostPair最后一个参数接收的是一个对象的引用，而list[i]非，故需要使用std::ref
        thr_list[i] = std::move(thr);
      }
      for(int i = 0;i < (int)thr_list.size();++i){    //统一join等待
        thr_list[i].join();
      }
      return true;
    }

    //3. 显示在线主机列表
    bool ShowOnlineHost(){
      for(int i = 0;i < (int)online_list.size();++i){
        std::cout << i << ". " << online_list[i] << "\n";
      }
      std::cout << "please choose: ";
      fflush(stdout);
      std::cin >> host_index;
      if(host_index < 0 || host_index > (int)online_list.size()){
        //host_index = -1;
        std::cerr << "choose error\n";
        return false;
      }
      return true;
    }

    //4. 显示在线主机,输入下标就可以获取列表
    bool GetFileList(){      
      Client client(online_list[host_index].c_str(),srv_port);
      auto rsp = client.Get("/list");  //请求路径/list
      //auto rsp = client.Get("./list");  //请求路径./list
      if(rsp && rsp->status == 200){
        boost::split(file_list,rsp->body,boost::is_any_of("\n"));   //切割正文
      }
      return true;
    }

    //5. 显示文件列表
    bool ShowFileList(std::string& name){ 
      for(int i = 0;i < (int)file_list.size();++i){
        std::cout << i << ". " << file_list[i] << "\n";
      }
      std::cout << "please choose:";
      fflush(stdout);
      int file_idx;
      std::cin >> file_idx;
      if(file_idx < 0 || file_idx > (int)file_list.size()){
        std::cerr << "choose error\n";
        return false;
      }
      name = file_list[file_idx];
      return true;
    }

    void RangeDownLoad(std::string host,std::string name,int64_t start,int64_t end,int* res){
      std::string uri = "/list/" + name;
      std::string realpath = "Download/" + name;
      std::stringstream range_val;
      range_val << "bytes=" << start << "-" << end;

      std::cerr << "download range: " << range_val.str() << "\n";
      *res = 0;
      Client client(host.c_str(),srv_port);
      //Range:bytes=start-end   组织头部
      Headers header;
      header.insert(std::make_pair("Range",range_val.str().c_str()));
      auto rsp = client.Get(uri.c_str(),header);
      if(rsp && rsp->status == 206){    //分块的正确传输返回码为 206
        int fd = open(realpath.c_str(),O_CREAT | O_WRONLY,0664);
        if(fd < 0){
          std::cerr << "file "<< realpath << "open error\n";
          return;
        }
        lseek(fd,start,SEEK_SET);     //偏移量，从起点开始偏移
        int ret = write(fd,&rsp->body[0],rsp->body.size());
        if(ret < 0){
          std::cerr << "file " << realpath << "write error\n";
          close(fd);
          return;
        }
        close(fd);
        *res = 1;
        std::cerr << "file " << realpath << "download range:";
        std::cerr <<  range_val.str() << " success\n";
      }
      return;
    }

    //获取文件大小 
    int64_t GetFileSize(std::string& host,std::string& name){
      int64_t fsize = -1;
      Client client(host.c_str(),srv_port); //实例化一个client对象
      std::string path = "/list/" + name;
      auto rsp = client.Head(path.c_str());      //获取head请求
      if(rsp && rsp->status == 200){
        if(!rsp->has_header("Content-Length"))  //存在返回真 不存在返回假
          return -1;

        //存在就获取正文长度
        std::string len = rsp->get_header_value("Content-Length");

        std::stringstream tmp;
        tmp << len;
        tmp >> fsize;
      }
      return fsize;
    }

    //6. 下载指定文件
    bool DownLoadFile(std::string& name){ 
      //1. 获取文件总长度
      //2. 根据文件总长度和分块大小分割线程的下载区域
      //3. 创建线程下载指定文件的指定分块数据
      //4. 同步等待所有线程结束,获取下载结果

      std::string host = online_list[host_index];
      int64_t fsize = GetFileSize(host,name);

      if(fsize < 0){
        std::cerr << "download file " << name << " failed\n";
        return false;
      }
      bool ret = true;
      int count = fsize / RANGE_SIZE;
      std::vector<boost::thread> thr_list(count+1);   //+1处理最后一个分块
      std::vector<int> res_list(count+1);  

      //2. 计算分块个数
      //等于 表示就算不够一个分块也要当做一个传输
      //1000: 0~299  300~599 600~899 900~999
      for(int64_t i = 0;i <= count;++i){
        int64_t start,end,range_len;
        start = i * RANGE_SIZE;
        end = (i+1)*RANGE_SIZE - 1;
        if(i == count){
          if((fsize % RANGE_SIZE) == 0){    //如果整除，表示没有最后一个分块
            break;
          }
          end = fsize - 1;
        }
        std::cerr << "range: " << start <<"-" << end << "\n";
        range_len = end - start + 1;

        //创建线程
        //使用boost库thread，因为std线程库不支持传递多参数

        //Range:bytes=start-end
        int* res = &res_list[i];
        boost::thread thr(&P2PClient::RangeDownLoad,this,host,name,start,end,res);
        //thr.join();
        //if(res == false){
        //  ret = false;
        //}
        thr_list[i] = std::move(thr);
      }
      for(int i = 0;i <= count;++i){
        if(i == count && fsize % RANGE_SIZE == 0){    //当处理到最后一个分块，并且整除，就没有，也不等待最后一个分块
          break;
        }
        thr_list[i].join(); //等待线程退出
        if(res_list[i] == 0){   //判断任一分块下载传输是否成功
          //std::cerr << "Range " << i << "download failded\n";
          ret = false;
        }
      }
      if(ret == true){
        std::cerr << "download file " << name << " success\n";
      }
      else{
        std::cerr << "download file " << name << " failed\n";
        return false;
      }
      return true;
    }

    //界面
    int DoFace(){
      std::cout << "1. " << "搜索附近主机" << std::endl;
      std::cout << "2. " << "显示在线主机" << std::endl;
      std::cout << "3. " << "显示文件列表" << std::endl;
      std::cout << "0. " << "退出" << std::endl;
      std::cout << "please choose: ";
      int choose;
      fflush(stdout);
      std::cin >> choose;
      return choose;
    }
  public:
    //构造函数 
    P2PClient(uint16_t port) 
      :srv_port(port)
    {}

    bool Start(){
      while(1){
        int choose = DoFace();
        //if(choose == 0){
        //  std::cerr << "GoodBye~" << std::endl;
        //  return false;
        //}
        std::vector<std::string> list;
        std::string filename;
        switch(choose){
          case 1:  
            if(GetAllHost(list)){
              GetOnlineHost(list);
              break;
            }
          case 2:
            if(ShowOnlineHost() == false){
              break;
            }
            GetFileList();
            break;
          case 3:   
            if(ShowFileList(filename) == false){
              break;
            }
            DownLoadFile(filename);
            break;
          case 0:
            exit(0);
          default:
            break;
        }
      }
    }
};
#endif
