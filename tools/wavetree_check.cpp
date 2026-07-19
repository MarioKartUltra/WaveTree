#include "../Source/dsp/Dwt.h"
#include "../Source/dsp/WaveletSynth.h"
#include <cstdio>
#include <cmath>
#include <vector>
using namespace wavetree;

static void prTest(int& fails){
    const int N=1024, L=6;
    for(int wt=0; wt<4; ++wt){
        Wavelet w = makeWavelet((WaveletType)wt);
        Dwt dwt; dwt.configure(w, N, L);
        std::vector<double> x(N), c(N), xr(N);
        unsigned s=12345u+wt;
        for(int i=0;i<N;++i){ s=s*1664525u+1013904223u; x[i]=((s>>9)*(1.0/8388608.0))-1.0; }
        dwt.forward(x.data(), c.data()); dwt.inverse(c.data(), xr.data());
        double mx=0; for(int i=0;i<N;++i) mx=std::fmax(mx,std::fabs(x[i]-xr[i]));
        printf("  PR %-14s max-err=%.2e\n", w.name.c_str(), mx);
        if(mx>1e-9) fails++;
    }
}

static double dom(const std::vector<float>&v,double sr){ // crude autocorr pitch
    int n=(int)v.size(); double mean=0; for(float x:v)mean+=x; mean/=n;
    int lo=(int)(sr/2000), hi=(int)(sr/50); double best=-1; int bl=0;
    for(int lag=lo;lag<hi&&lag<n;++lag){double a=0; for(int i=0;i+lag<n;++i)a+=(v[i]-mean)*(v[i+lag]-mean); if(a>best){best=a;bl=lag;}}
    return bl>0?sr/bl:0;
}

int main(){
    int fails=0;
    printf("== perfect reconstruction ==\n"); prTest(fails);
    printf("\n== render each mode (note 57 = 220Hz) ==\n");
    const double sr=48000; const int block=256; const int total=(int)(1.5*sr);
    const char* mn[3]={"Bands","Cascade","Atoms"};
    for(int m=0;m<3;++m){
        WaveTreeParams p; p.mode=(Mode)m; p.wavelet=WaveletType::Db4;
        WaveletSynth syn; syn.prepare(sr); syn.setParameters(p);
        std::vector<float> out(total,0), L(block),R(block);
        bool on=false, finite=true; double peak=0,ss=0;
        for(int nprocessed=0;nprocessed<total;){
            if(!on){ syn.noteOn(57,0.9); on=true; }
            if(nprocessed> (int)(1.1*sr)) syn.noteOff(57);
            int mm=std::min(block,total-nprocessed);
            syn.renderBlock(L.data(),R.data(),mm);
            for(int i=0;i<mm;++i){ float s=L[i]; if(!std::isfinite(s))finite=false; out[nprocessed+i]=s; double a=std::fabs(s); if(a>peak)peak=a; ss+=(double)s*s; }
            nprocessed+=mm;
        }
        std::vector<float> win(out.begin()+(int)(0.2*sr), out.begin()+(int)(0.6*sr));
        printf("  %-8s peak=%.3f rms=%.4f domFreq=%.1f %s\n", mn[m], peak, std::sqrt(ss/total), dom(win,sr), finite?"":"NaN!");
        if(!finite||peak<1e-4) fails++;
    }
    printf("\n%d issue(s)\n", fails);
    return fails;
}
