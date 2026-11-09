#include "meshoptimizer.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int argc,char** argv) {
 if(argc!=4)return 1;
 FILE* f=fopen(argv[1],"rb"); if(!f)return 2;
 unsigned count,indices;
 if(fread(&count,4,1,f)!=1||fread(&indices,4,1,f)!=1)return 3;
 std::vector<float> point(count*3);std::vector<unsigned> tri(indices),out(indices);
 if(fread(point.data(),12,count,f)!=count||fread(tri.data(),4,indices,f)!=indices)return 4;
 fclose(f);
 std::vector<unsigned char> lock(count,0);f=fopen(argv[3],"rb");if(f){if(fread(lock.data(),1,count,f)!=count)return 6;fclose(f);}
 float err;size_t n=meshopt_simplifyWithAttributes(out.data(),tri.data(),indices,point.data(),count,12,nullptr,0,nullptr,0,lock.data(),18000*3,0.14f,meshopt_SimplifyErrorAbsolute,&err);
 f=fopen(argv[2],"wb");if(!f)return 5;fwrite(out.data(),4,n,f);fclose(f);
 printf("%zu faces, %.4fm bounded meshopt error\n",n/3,err);return 0;
}
