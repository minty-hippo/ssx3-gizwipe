#include <stdio.h>
#include <stdlib.h>
#include "track.h"
int main(int c, char **v){ Track t; track_load(&t, v[1]); float s=atof(v[2]); track_update_stream(&t,s,99);
 int x0=atoi(v[3]),z0=atoi(v[4]),x1=atoi(v[5]),z1=atoi(v[6]); int N=40;
 for(int i=0;i<=N;i++){ s32 x=x0+(x1-x0)*i/N, z=z0+(z1-z0)*i/N; s32 a[3]={x,40000,z},b[3]={x,-40000,z},h[3],n[3]; int sf;
  if(track_ray(&t,a,b,h,n,&sf)) printf("%3d (%6d,%6d) y=%6d surf=%d n=(%6d %6d %6d)\n",i,x,z,h[1],sf,n[0],n[1],n[2]); else printf("%3d miss\n",i);} }
