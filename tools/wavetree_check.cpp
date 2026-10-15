#include "../Source/dsp/Dwt.h"
#include "../Source/dsp/Mfa.h"
#include "../Source/dsp/CoeffGen.h"
#include "../Source/dsp/WaveletSynth.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
using namespace wavetree;

static void prTest(int& fails){
    const int N=1024, L=6;
    int biorthogonal=0;
    for(int wt=0; wt<kNumWavelets; ++wt){
        Wavelet w = makeWavelet((WaveletType)wt);
        Dwt dwt; dwt.configure(w, N, L);
        std::vector<double> x(N), c(N), xr(N);
        unsigned s=12345u+wt;
        for(int i=0;i<N;++i){ s=s*1664525u+1013904223u; x[i]=((s>>9)*(1.0/8388608.0))-1.0; }
        dwt.forward(x.data(), c.data()); dwt.inverse(c.data(), xr.data());
        double mx=0; for(int i=0;i<N;++i) mx=std::fmax(mx,std::fabs(x[i]-xr[i]));
        // every family must reconstruct exactly: the orthogonal ones through the
        // adjoint, the biorthogonal ones through their dual synthesis filters
        double sumLo=0; for(double v: w.lo) sumLo+=v;
        printf("  PR %-28s taps=%2d %s max-err=%.2e sum(lo)=%.6f\n", w.name.c_str(), w.taps(),
               w.orthogonal ? "orthogonal  " : "biorthogonal", mx, sumLo);
        if(mx>1e-9) fails++;
        if(std::fabs(sumLo-std::sqrt(2.0))>1e-9) fails++;                 // unity DC gain
        if((int)w.slo.size()!=w.taps() || (int)w.shi.size()!=w.taps()) fails++;
        if(w.orthogonal){                                                   // synthesis == analysis taps
            for(int k=0;k<w.taps();++k) if(w.slo[(size_t)k]!=w.lo[(size_t)k] || w.shi[(size_t)k]!=w.hi[(size_t)k]) { fails++; break; }
        } else ++biorthogonal;
    }
    if(biorthogonal!=3){ printf("  expected three biorthogonal families, found %d\n", biorthogonal); fails++; }
    // the first four keep their old tables bit for bit (old projects sound the same)
    if(makeWavelet(WaveletType::Db2).lo[0]!=0.48296291314469025 || makeWavelet(WaveletType::Db6).taps()!=12) fails++;
    if(std::string(waveletName(WaveletType::Bior44))!="Bior4.4" || std::string(waveletName(WaveletType::Coif2))!="Coif2") fails++;
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

    { TreeEdit t; t.setNodeLevel(0,1.0f,0.0f); applyTreeEdit(t,in,out,nb,off,size);
      check(near(std::fabs(out[0]),2.0*peak),"a coefficient at zero painted to the top comes in at twice the tree's peak"); }

    { TreeEdit t; t.setNodeLevel(4,0.9f,0.0f); applyTreeEdit(t,in,out,nb,off,size);
      check(near(std::fabs(out[4]),TreeEdit::levelToMagnitude(0.9f,peak)) && std::fabs(out[4])>0.49*peak,
            "a coefficient at zero painted to the height of the peak is as loud as the peak"); }

    { TreeEdit t; t.setNodeLevel(5,0.5f,0.0f); applyTreeEdit(t,in,out,nb,off,size);
      check(near(std::fabs(out[5]),TreeEdit::levelToMagnitude(0.5,peak)),"a nearly muted coefficient takes the painted level");
      check(out[5]<0.0,"a painted coefficient keeps the sign the generator gave it"); }

    { TreeEdit t; t.setNodeLevel(1,0.02f,0.0f); applyTreeEdit(t,in,out,nb,off,size);
      check(out[1]==0.0,"painting at the very bottom mutes"); }

    { TreeEdit t; t.setNodeLevel(0,1.0f,0.0f); t.setNodeLevel(2,0.9f,0.0f); applyTreeEdit(t,in,out,nb,off,size);
      check(near(std::fabs(out[2]),TreeEdit::levelToMagnitude(0.9f,peak)),
            "painting one node above the peak does not move the scale for the others"); }

    { TreeEdit t; t.setNodeLevel(3,0.8f,0.0f); check(std::fabs(t.getNodeLevel(3)-0.8f)<1e-6f,"a painted level can be read back");
      t.clearNode(3); applyTreeEdit(t,in,out,nb,off,size);
      check(out[3]==in[3] && t.getNodeLevel(3)==TreeEdit::kUnset,"erasing a node gives the generated value back");
      check(t.isIdentity(),"a tree with every edit erased is untouched again"); }

    { TreeEdit t; t.setNodeLevel(6,0.7f,0.0f); check(!t.isIdentity(),"a painted tree is not untouched"); }

    // a painted level is a height on the WHOLE tree's scale (what is drawn is what is
    // heard), and it remembers the slope it was painted at: when the slope moves, the
    // node moves with the tree's own decay at its scale (band 0 holds the peak here)
    { TreeEdit t; t.setNodeLevel(0,0.9f,1.0f); t.setNodeLevel(4,0.9f,1.0f); applyTreeEdit(t,in,out,nb,off,size,1.0);
      check(near(std::fabs(out[4]),TreeEdit::levelToMagnitude(0.9f,peak)),"painted and played at one slope: the whole-tree scale, in any band");
      check(near(std::fabs(out[0]),std::fabs(out[4])),"equal heights are equal magnitudes"); }
    { TreeEdit t; t.setNodeLevel(0,0.9f,1.0f); t.setNodeLevel(4,0.9f,1.0f); applyTreeEdit(t,in,out,nb,off,size,2.0);
      check(near(std::fabs(out[0]),TreeEdit::levelToMagnitude(0.9f,peak)),"the peak's band does not move with the slope");
      check(near(std::fabs(out[4]),0.5*TreeEdit::levelToMagnitude(0.9f,peak)),"one band finer and one more octave of slope: half as loud"); }
    { TreeEdit t; t.setNodeLevel(4,0.02f,1.0f); applyTreeEdit(t,in,out,nb,off,size,0.0);
      check(out[4]==0.0,"a muted node stays muted whatever the slope"); }
    { TreeEdit t; t.setNodeLevel(4,0.7f,1.3f); check(std::fabs(t.getNodeSlope(4)-1.3f)<1e-6f,"the paint slope is kept with the node");
      check(near(paintedMagnitude(0.7f,1.3f,peak,0,1,(double)1.3f),TreeEdit::levelToMagnitude(0.7f,peak)),"paintedMagnitude: unmoved slope = the drawn level"); }
    // the slope each mode paints with
    { WaveTreeParams q; q.mode=Mode::Bands; q.tilt=0.5; check(near(treeSlope(q),1.1),"Bands: tilt x 2.2");
      q.mode=Mode::Cascade; q.hurst=0.6; q.tilt=0.5; check(near(treeSlope(q),cascadeSlope(0.6)),"Cascade at tilt 50 %: the Hurst slope, as before");
      q.tilt=1.0; check(near(treeSlope(q),cascadeSlope(0.6)+1.1),"Cascade: tilt above 50 % steepens");
      q.mode=Mode::Atoms; q.tilt=0.0; check(near(treeSlope(q),0.7-1.1),"Atoms: 0.7 plus the tilt offset"); }
    // and the Cascade generator follows tilt too (sigma 0: a pure power law)
    { Dwt dwt; dwt.configure(makeWavelet(WaveletType::Db4), 2048, 10);
      CoeffScratch sc; sc.prepare(2048);
      std::vector<double> c(2048);
      auto ampRatio=[&](double tilt){ WaveTreeParams q; q.mode=Mode::Cascade; q.hurst=0.5; q.intermittency=0.0; q.tilt=tilt;
          Rng rng(7); generateCoeffs(c.data(), dwt, q, rng, sc);
          return std::fabs(c[(size_t)dwt.bandOff(2)]) / std::fabs(c[(size_t)dwt.bandOff(6)]); };   // four bands apart
      const double flat=ampRatio(0.5), steep=ampRatio(1.0);
      check(near(flat, std::pow(2.0, 4.0*cascadeSlope(0.5))),"tilt 50 %: the amplitude law is the Hurst slope alone");
      check(near(steep, std::pow(2.0, 4.0*(cascadeSlope(0.5)+1.1))),"tilt 100 %: 1.1 per band steeper"); }

    { TreeEdit t; t.setNodeGain(7,0.5f); applyTreeEdit(t,in,out,nb,off,size);
      check(near(out[7],0.5*in[7]),"a gain saved by an older project still multiplies"); }

    { TreeEdit t; t.setBandGain(1,0.5f); t.setNodeLevel(4,1.0f,0.0f); applyTreeEdit(t,in,out,nb,off,size);
      check(near(std::fabs(out[4]),peak),"the band drawbar scales a painted node too"); }

    { TreeEdit t; t.setNodeLevel(2,0.6f,0.0f); t.reset();
      check(t.isIdentity() && t.getNodeLevel(2)==TreeEdit::kUnset,"reset clears painted levels"); }

    check(std::fabs(TreeEdit::magnitudeToLevel(peak,peak)-0.9)<1e-12,"the tree's peak sits at 90% of a row");
    bool round=true;
    for(double l=0.05;l<=1.0;l+=0.05) round = round && std::fabs(TreeEdit::magnitudeToLevel(TreeEdit::levelToMagnitude(l,peak),peak)-l)<1e-9;
    check(round,"level to magnitude and back is the same level");
    check(TreeEdit::magnitudeToLevel(0.0,peak)==0.0 && TreeEdit::levelToMagnitude(0.5,0.0)==0.0,"zero in gives zero out");
}


// D(h): the chord estimator and the analytic Cascade spectrum
static void dhTest(int& fails){
    const int N=4096, L=8;                     // 9 bands: coarse 16 .. fine 2048
    auto bandSize=[&](int b){ return b==0 ? (N>>L) : (N>>(L-b+1)); };
    auto bandOff =[&](int b){ int o=N>>L; for(int i=1;i<b;++i) o+=bandSize(i); return b==0?0:o; };
    // a synthetic cascade tree: |d| = 2^{-j s} e^{lambda}, lambda a Gaussian walk down the tree
    auto make=[&](double s, double sigma, unsigned seed){
        std::vector<float> c((size_t)N,0.0f); std::vector<double> prev, cur;
        unsigned st=seed; auto gauss=[&]{ double u=0; for(int i=0;i<12;++i){ st=st*1664525u+1013904223u; u+=(st>>8)*(1.0/16777216.0);} return u-6.0; };
        for(int b=1;b<=L;++b){ int off=bandOff(b), sz=bandSize(b); cur.assign((size_t)sz,0.0);
            double A=std::pow(2.0,-(double)(b-1)*s);
            for(int k=0;k<sz;++k){ double parent=(b==1)?0.0:prev[(size_t)(k/2)]; double lm=parent+sigma*gauss(); cur[(size_t)k]=lm; c[(size_t)(off+k)]=(float)(A*std::exp(lm)); }
            std::swap(prev,cur); }
        return c; };
    const auto a=make(1.0,0.3,7u), b=make(1.0,0.3,9u), c2=make(1.8,0.3,11u);
    const auto one=computeDh(a.data(), L+1, bandOff, bandSize);
    const float* two[2]={a.data(), a.data()};
    const auto same=computeDhMulti(two, 2, L+1, bandOff, bandSize);
    double worst=0; for(int i=0;i<one.count;++i){ worst=std::fmax(worst, std::fabs(one.h[i]-same.h[i])); worst=std::fmax(worst, std::fabs(one.Dh[i]-same.Dh[i])); }
    printf("  chord of one voice twice == that voice: max diff %.1e\n", worst);
    if(!(one.valid && same.valid && worst<1e-9)) fails++;
    // the estimator finds the generator's centre: h* near s (sigma small: the walk barely spreads it)
    printf("  single voice s=1.0: h* %.3f  [%.3f, %.3f]\n", one.hPeak, one.hMin, one.hMax);
    if(std::fabs(one.hPeak-1.0)>0.25) fails++;
    // two voices with different slopes: the chord's arch spans both
    const float* mix[2]={a.data(), c2.data()};
    const auto lo=computeDh(a.data(), L+1, bandOff, bandSize), hi=computeDh(c2.data(), L+1, bandOff, bandSize);
    const auto ch=computeDhMulti(mix, 2, L+1, bandOff, bandSize);
    printf("  voices s=1.0 / s=1.8: peaks %.3f / %.3f, chord %.3f range [%.3f, %.3f] vs union [%.3f, %.3f]\n",
           lo.hPeak, hi.hPeak, ch.hPeak, ch.hMin, ch.hMax, std::fmin(lo.hMin,hi.hMin), std::fmax(lo.hMax,hi.hMax));
    if(!(ch.valid && ch.hPeak>lo.hPeak-0.1 && ch.hPeak<hi.hPeak+0.1)) fails++;
    if(!(ch.hMin < lo.hMin+0.35 && ch.hMax > hi.hMax-0.35)) fails++;
    // the analytic parabola: 1 at the centre, symmetric, narrower with less intermittency
    const double s0=cascadeSlope(0.5);
    if(std::fabs(analyticDh(0.5,0.4,s0)-1.0)>1e-12) fails++;
    if(std::fabs(analyticDh(0.5,0.4,s0+0.3)-analyticDh(0.5,0.4,s0-0.3))>1e-12) fails++;
    if(!(analyticDh(0.5,0.2,s0+0.3) < analyticDh(0.5,0.4,s0+0.3))) fails++;
    if(std::fabs(cascadeSlope(0.0)-0.35)>1e-12 || std::fabs(cascadeSlope(1.0)-1.95)>1e-12 || std::fabs(cascadeSigma(1.0)-0.9)>1e-12) fails++;
    // zero intermittency: a point
    if(!(analyticDh(0.5,0.0,s0)==1.0 && analyticDh(0.5,0.0,s0+0.01) < -1.0)) fails++;
    printf("  analytic: D(s)=%.3f, D(s+0.3)=%.3f at I=0.4, %.3f at I=0.2\n", analyticDh(0.5,0.4,s0), analyticDh(0.5,0.4,s0+0.3), analyticDh(0.5,0.2,s0+0.3));
}

// The keyboard's note stack: a key that was cut by a later key comes back when
// that key is released; legato keeps the envelope and table across the change;
// glide slides the pitch from the previous note over a set time.
static void noteStackTest(int& fails){
    const double sr=48000; std::vector<float> L(256),R(256);
    auto run=[&](WaveletSynth& s,int blocks){ for(int b=0;b<blocks;++b) s.renderBlock(L.data(),R.data(),256); };
    const double hzA=440*std::pow(2.0,(48-69)/12.0), hzB=2*hzA;
    // 1. mono return: hold A, press B cuts A, release B returns to A without a new key press
    { WaveTreeParams p; p.numVoices=1; p.seedMode=SeedModeP::Fixed; WaveletSynth s; s.prepare(sr); s.setParameters(p);
      s.noteOn(48,0.9); run(s,4); s.noteOn(55,0.9); run(s,4);
      const auto& v=s.voice(0);
      bool ok = v.isActive() && !v.isReleased() && v.midiNote()==55;
      s.noteOff(55); run(s,4);
      ok = ok && v.isActive() && !v.isReleased() && v.midiNote()==48;
      s.noteOff(48); run(s,4);
      ok = ok && v.isReleased();
      printf("  mono return: %s\n", ok?"ok":"FAIL"); if(!ok) fails++; }
    // 2. legato: the envelope keeps going; without it the second key restarts the attack
    for(int legato=0; legato<2; ++legato){
      WaveTreeParams p; p.numVoices=1; p.legato=legato!=0; p.attack=0.2; p.sustain=1.0; WaveletSynth s; s.prepare(sr); s.setParameters(p);
      s.noteOn(48,0.9); run(s,10);                                   // 53 ms into a 200 ms attack
      const double before=s.voice(0).envelope();
      s.noteOn(55,0.9); s.renderBlock(L.data(),R.data(),1);
      const double after=s.voice(0).envelope();
      const bool ok = legato ? (after>=before-1e-6 && s.voice(0).midiNote()==55) : (after<0.01);
      printf("  legato=%d: env %.3f -> %.3f %s\n", legato, before, after, ok?"ok":"FAIL"); if(!ok) fails++;
    }
    // 3. glide: the new note starts at the old pitch and arrives after the glide time
    { WaveTreeParams p; p.numVoices=1; p.glideSeconds=0.1; WaveletSynth s; s.prepare(sr); s.setParameters(p);
      s.noteOn(48,0.9); run(s,4); s.noteOn(60,0.9);
      s.renderBlock(L.data(),R.data(),64);
      const double start=s.voice(0).currentHz();
      run(s,10);                                                     // 53 ms: on the way
      const double mid=s.voice(0).currentHz();
      run(s,12);                                                     // past 100 ms: arrived
      const double end=s.voice(0).currentHz();
      const bool ok = std::fabs(start/hzA-1)<0.03 && mid>hzA*1.2 && mid<hzB*0.85 && std::fabs(end/hzB-1)<1e-6 && !s.voice(0).isGliding();
      printf("  glide 100 ms: %.1f -> %.1f -> %.1f Hz (A=%.1f, B=%.1f) %s\n", start, mid, end, hzA, hzB, ok?"ok":"FAIL"); if(!ok) fails++;
      // no glide: the new note starts on pitch
      WaveTreeParams q; q.numVoices=1; WaveletSynth t; t.prepare(sr); t.setParameters(q);
      t.noteOn(48,0.9); run(t,4); t.noteOn(60,0.9); t.renderBlock(L.data(),R.data(),1);
      const bool ok2 = std::fabs(t.voice(0).currentHz()/hzB-1)<1e-6 && !t.voice(0).isGliding();
      printf("  glide off: %.1f Hz %s\n", t.voice(0).currentHz(), ok2?"ok":"FAIL"); if(!ok2) fails++; }
    // 4. poly: three held keys on two voices; the oldest is cut and comes back when the thief is released
    { WaveTreeParams p; p.numVoices=2; WaveletSynth s; s.prepare(sr); s.setParameters(p);
      auto sounding=[&](int note){ for(int i=0;i<2;++i) if(s.voice(i).isActive()&&!s.voice(i).isReleased()&&s.voice(i).midiNote()==note) return true; return false; };
      s.noteOn(48,0.9); run(s,2); s.noteOn(52,0.9); run(s,2); s.noteOn(55,0.9); run(s,2);
      bool ok = !sounding(48) && sounding(52) && sounding(55);
      s.noteOff(55); run(s,2);
      ok = ok && sounding(48) && sounding(52) && !sounding(55);
      s.noteOff(52); run(s,2);
      ok = ok && sounding(48) && !sounding(52);                     // nothing waiting: a plain release
      printf("  poly steal + return: %s\n", ok?"ok":"FAIL"); if(!ok) fails++; }
    // 5. legato in Per-Key mode moves the tree to the new key's seed (crossfaded); Fixed keeps the table
    for(int mode=0; mode<2; ++mode){
      WaveTreeParams p; p.numVoices=1; p.legato=true; p.seedMode= mode? SeedModeP::PerKey : SeedModeP::Fixed;
      WaveletSynth s; s.prepare(sr); s.setParameters(p); s.noteOn(48,0.9); run(s,4);
      s.noteOn(55,0.9); const bool pending=s.voice(0).hasPendingRegen();
      const bool ok = pending == (mode==1);
      printf("  legato reseed (%s): pending=%d %s\n", mode?"per-key":"fixed", (int)pending, ok?"ok":"FAIL"); if(!ok) fails++;
    }
}

int main(){
    int fails=0;
    printf("== note stack, legato, glide ==\n"); noteStackTest(fails);
    printf("\n== tree painting ==\n"); treeEditTest(fails);
    printf("\n== perfect reconstruction ==\n"); prTest(fails);
    printf("\n== D(h): chord and analytic ==\n"); dhTest(fails);
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
