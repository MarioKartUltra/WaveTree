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

// Painting the tree: a level is set, not a multiplier, so a coefficient the
// generator left at zero can be painted in.
static void treeEditTest(int& fails){
    auto check=[&](bool ok,const char* what){ if(!ok){ printf("  FAIL %s\n",what); fails++; } };
    auto near=[](double a,double b){ return std::fabs(a-b) <= 1e-9*std::fmax(1.0,std::fabs(b)); };

    // two bands of four: band 0 = nodes 0..3, band 1 = nodes 4..7
    const int nb=2; auto off=[](int b){ return b*4; }; auto size=[](int){ return 4; };
    //                 0      1     2      3       4     5      6       7
    const double in[8]={ 0.0, -0.5,  0.25,  1e-7,  0.0, -1e-6,  0.125, -0.2 };
    const double peak=treePeakOf(in,8);
    double out[8];
    check(near(peak,0.5),"the reference is the largest generated magnitude");

    { TreeEdit t; applyTreeEdit(t,in,out,nb,off,size);
      bool same=true; for(int i=0;i<8;++i) same = same && out[i]==in[i];
      check(same,"an untouched tree passes the coefficients through unchanged"); }

    { TreeEdit t; t.setNodeLevel(0,1.0f); applyTreeEdit(t,in,out,nb,off,size);
      check(near(std::fabs(out[0]),2.0*peak),"a coefficient at zero painted to the top comes in at twice the tree's peak"); }

    { TreeEdit t; t.setNodeLevel(4,0.9f); applyTreeEdit(t,in,out,nb,off,size);
      check(near(std::fabs(out[4]),TreeEdit::levelToMagnitude(0.9f,peak)) && std::fabs(out[4])>0.49*peak,
            "a coefficient at zero painted to the height of the peak is as loud as the peak"); }

    { TreeEdit t; t.setNodeLevel(5,0.5f); applyTreeEdit(t,in,out,nb,off,size);
      check(near(std::fabs(out[5]),TreeEdit::levelToMagnitude(0.5,peak)),"a nearly muted coefficient takes the painted level");
      check(out[5]<0.0,"a painted coefficient keeps the sign the generator gave it"); }

    { TreeEdit t; t.setNodeLevel(1,0.02f); applyTreeEdit(t,in,out,nb,off,size);
      check(out[1]==0.0,"painting at the very bottom mutes"); }

    { TreeEdit t; t.setNodeLevel(0,1.0f); t.setNodeLevel(2,0.9f); applyTreeEdit(t,in,out,nb,off,size);
      check(near(std::fabs(out[2]),TreeEdit::levelToMagnitude(0.9f,peak)),
            "painting one node above the peak does not move the scale for the others"); }

    { TreeEdit t; t.setNodeLevel(3,0.8f); check(std::fabs(t.getNodeLevel(3)-0.8f)<1e-6f,"a painted level can be read back");
      t.clearNode(3); applyTreeEdit(t,in,out,nb,off,size);
      check(out[3]==in[3] && t.getNodeLevel(3)==TreeEdit::kUnset,"erasing a node gives the generated value back");
      check(t.isIdentity(),"a tree with every edit erased is untouched again"); }

    { TreeEdit t; t.setNodeLevel(6,0.7f); check(!t.isIdentity(),"a painted tree is not untouched"); }

    { TreeEdit t; t.setNodeGain(7,0.5f); applyTreeEdit(t,in,out,nb,off,size);
      check(near(out[7],0.5*in[7]),"a gain saved by an older project still multiplies"); }

    { TreeEdit t; t.setBandGain(1,0.5f); t.setNodeLevel(4,1.0f); applyTreeEdit(t,in,out,nb,off,size);
      check(near(std::fabs(out[4]),peak),"the band drawbar scales a painted node too"); }

    { TreeEdit t; t.setNodeLevel(2,0.6f); t.reset();
      check(t.isIdentity() && t.getNodeLevel(2)==TreeEdit::kUnset,"reset clears painted levels"); }

    check(std::fabs(TreeEdit::magnitudeToLevel(peak,peak)-0.9)<1e-12,"the tree's peak sits at 90% of a row");
    bool round=true;
    for(double l=0.05;l<=1.0;l+=0.05) round = round && std::fabs(TreeEdit::magnitudeToLevel(TreeEdit::levelToMagnitude(l,peak),peak)-l)<1e-9;
    check(round,"level to magnitude and back is the same level");
    check(TreeEdit::magnitudeToLevel(0.0,peak)==0.0 && TreeEdit::levelToMagnitude(0.5,0.0)==0.0,"zero in gives zero out");
}

int main(){
    int fails=0;
    printf("== tree painting ==\n"); treeEditTest(fails);
    printf("\n== perfect reconstruction ==\n"); prTest(fails);
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
