
#ifndef __M_SRV_H__
#define __M_SRV_H__	

#include <iostream>
#include <fstream>  
#include <stdio.h>  
#include "httplib.h"
#include <boost/filesystem.hpp> 
//使用boost库是为了可移植性，在windows下可用，因为如果系统调用多了，其他OS就用不了

using namespace httplib;
namespace bf = boost::filesystem;

//对于服务端的共享目录,也作为下载的存放目录
#define SHARED_PATH "Shared"

//#define LOG(fmt, ...) std::cout << fprintf(stderr, fmt, __VA_ARGS__)  //打印错误信息

class P2PServer{
  private:
    Server _server;

  private:
    //static 是因为否则有三个参数
    
    static void GetHostPair(const Request& req,Response& rsp){    //主机配对
      rsp.status = 200;
    }

    static void GetFileList(const Request& req,Response& rsp){
      bf::directory_iterator item_begin(SHARED_PATH);
      bf::directory_iterator item_end;  //不传参默认是迭代器结束位置
      std::stringstream body;
      //body << "<html><body>";
      for(;item_begin != item_end;++item_begin){
        if(bf::is_directory(item_begin->status())){ //如果是目录，跳过
          continue;
        }
          //获取文件名称，不包含路径
        //bf::path path = item_begin->path().c_str(); //后者返回的是filesystem对象
        //varstd::string name = path.filename().string();  //不加.string()会给文名加上一个""
        std::string name = item_begin->path().filename().string();
        rsp.body += name + "\n";  //文件列表格式,下面这个格式也可以
        //body << "<h4><a href='/list/" << name << "'>";
        //body << name;
        //body << "</a></h4>";

      }
      //body << "</body></html>";
      //rsp.body = body.str();
      rsp.set_header("Content-Type","text/html");
      rsp.status = 200;
      //rsp.set_content(&body[0],body.size(),"text/html");
    }

    static void GetFileData(const Request& req,Response& rsp){
      // 请求的是/list/a.txt -> 转换成DownLoad/a.txt
      bf::path path(req.path);  //取出文件名
      std::stringstream name;
      name << SHARED_PATH << "/" << path.filename().string();

      //1. 文件不存在
      if(!bf::exists(name.str())){  
        rsp.status = 404; //没有找到
        return;
      }
      //2. 如果是一个目录文件
      if(bf::is_directory(name.str())){
        rsp.status = 403; //知道理解客户端请求，但是不允许，通常是权限问题
        return;
      }

      int64_t fsize = bf::file_size(name.str());    //文件大小

      if(req.method == "HEAD"){   //如果是head请求方法
        rsp.status = 200;
        rsp.set_header("Content-Length",std::to_string(fsize).c_str());
        return;   //对于head方法可以直接返回，不需要响应正文信息
      }
      else{   //否则是get方法
        //首先判断是否有range
        if(!req.has_header("Range")){     //没有range字段表示分块传输有问题
          rsp.status = 400;
          return;
        }

        //获取range信息
        std::string range_val;
        range_val = req.get_header_value("Range");

        //bytes=start-end
        int64_t start,rlen;
        bool ret = RangeParse(range_val,start,rlen);    //只是解析数据，不依赖任何对象
        if(ret == false){
          rsp.status = 400;
          return;
        }
        //获取range起始和结束逻辑完毕

        std::cerr << "body resize: " << rlen << "\n";
        rsp.body.resize(rlen);   //重新定义body大小
        std::ifstream file(name.str(),std::ios::binary);    //文件流操作

        if(!file.is_open()){   //判断是否打开
          std::cerr << "open file " << name.str() << "failed\n";  //写入标准错误，因为写入标准输出在终端上乱打印很不好
          rsp.status = 404;
          return;
        }

        file.seekg(start,std::ios::beg);
        //打开成功，读取数据
        file.read(&rsp.body[0],rlen);
        if(!file.good()){
          std::cerr << "read file "<< name.str() << "body error\n";
          rsp.status = 500;
          return;
        }

        //读完数据，关闭
        file.close();
        rsp.status = 206;
        //下载功能
        rsp.set_header("Content-Type","application/octet-stream");  //octet-stream表示二进制流
        std::cerr << "file range:" << range_val << "download success\n";
      }     
    }

    static bool RangeParse(std::string& range_val,int64_t& start,int64_t& len){
        size_t pos1 = range_val.find("=");
        size_t pos2 = range_val.find("-");
        if(pos1 == std::string::npos || pos2 == std::string::npos){     //格式不对
          std::cerr << "range " << range_val << "format error\n";
          return false;
        }
        //获取start与end
        int64_t end;
        std::string rstart;
        std::string rend;
        rstart = range_val.substr(pos1 + 1,pos2 - pos1 - 1);
        rend = range_val.substr(pos2 + 1);

        //字符串 转 数字
        std::stringstream tmp;
        tmp << rstart;
        tmp >> start;
        tmp.clear();
        tmp << rend;
        tmp >> end;
        len = end - start + 1;
        return true;
    }


  public:
    //构造函数
    P2PServer(){
      if(!bf::exists(SHARED_PATH)){   //判断共享目录若不存在,则创建
        bf::create_directory(SHARED_PATH);
      }
    }

    bool Start(uint16_t port){
      //收到不同get请求，调用不同回调函数
      _server.Get("/hostpair",GetHostPair);
      _server.Get("/list",GetFileList);
      _server.Get("/list/(.*)",GetFileData); //使用正则表达式
      //_server.Get("/list/filename/(.*)",GetFileData); //使用正则表达式
      _server.listen("0.0.0.0",port);
    
    return true;
    }
};
#endif
