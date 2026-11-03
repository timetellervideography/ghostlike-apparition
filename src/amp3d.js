/* ================= Amp3D: real-time WebGL render of the amp head =================
   Physically based materials, image-based lighting, procedural relief (height → displacement + normal map)
   with emissive "tube light" leaking through it. Knobs follow the shared state; transparent HTML hit areas
   on top handle the interaction. Falls back to the CSS amp when WebGL is not available. */
const Amp3D=(()=>{
const T=window.THREE;
const W=4.0, H=1.9, D=1.25, FR=.13, PH=.54;             // cabinet width/height/depth, frame, faceplate height
const IW=W-2*FR, RH=H-2*FR-PH;                          // inner width, relief height
const KX=[-1.02,-.68,-.34,0,.34,.68,1.02], KY=-H/2+FR+PH*.42; // knob x positions, knob centre y
const KEYS=['gain','bass','mid','treble','presence','depth','master'], LABELS=['GAIN','BASS','MIDDLE','TREBLE','PRESENCE','DEPTH','MASTER'];
const TX={ampTight:-1.42,ampBright:-1.27}, JX=-1.66, LOGOX=1.38, JEWX=1.74;
const PZ=D/2-.16;                                       // faceplate front z
let R,scene,cam,host,overlay,ok=false,dirty=true,glow=0,theme=null,built=null,raf=0;
const cache={};

/* ---------- the Ghostlike "g" (brand mark), as 2D path and as 3D shape ---------- */
const G_PATH='M170.1,69c0-6.6-5.7-11.1-12.3-11.1s-12.9,4.8-17.7,10.2c-5.7,6.6-11.1,12.3-21,13.8-11.1-3.9-24.3-6-39-6C34.5,75.9,1.2,96,1.2,130.2c0,20.1,12.6,33.3,31.5,41.1C15.6,179.1,0,191.1,0,209.7c0,12.3,8.7,34.8,50.4,34.8h56.7c12,0,18.6,3.6,18.6,12.9,0,8.7-5.7,18.9-22.5,18.9-3.3,0-6.9,1.5-6.9,5.7,0,4.5,5.1,6.3,10.5,6.3,40.8,0,60.9-25.8,60.9-49.2,0-26.1-18.9-42.6-63.3-42.6h-59.1c-9.9,0-13.5-3.3-13.5-8.1,0-5.1,4.2-9.6,9.6-14.1,11.1,3,23.4,4.5,36.3,4.5,44.1,0,78-17.7,78-49.2,0-18.6-9.3-33-25.2-42.3,26.7-1.5,39.6-6.6,39.6-18.3ZM80.1,167.7c-15.3,0-22.5-17.7-22.5-42s6.3-37.8,19.8-37.8c15.6,0,22.2,18,22.2,41.1,0,24.9-6.3,38.7-19.5,38.7Z';
function gShapes(){ // minimal SVG path parser (M,C,c,s,h,Z) → THREE shapes with the bowl as hole; y flipped, centred, height 1
  const tk=G_PATH.match(/[a-zA-Z]|-?\d*\.?\d+/g),sp=new T.ShapePath();let i=0,cmd='',x=0,y=0,lx=0,ly=0;
  const num=()=>parseFloat(tk[i++]),P=(a,b)=>[(a-86)/289,-(b-144)/289];
  while(i<tk.length){if(/[a-zA-Z]/.test(tk[i]))cmd=tk[i++];
    if(cmd==='M'){x=num();y=num();sp.moveTo(...P(x,y));cmd='L'}
    else if(cmd==='C'||cmd==='c'){const r=cmd==='c';let a=num(),b=num(),c=num(),d=num(),e=num(),f=num();if(r){a+=x;b+=y;c+=x;d+=y;e+=x;f+=y}sp.bezierCurveTo(...P(a,b),...P(c,d),...P(e,f));lx=c;ly=d;x=e;y=f}
    else if(cmd==='s'){let c=num()+x,d=num()+y,e=num()+x,f=num()+y;sp.bezierCurveTo(...P(2*x-lx,2*y-ly),...P(c,d),...P(e,f));lx=c;ly=d;x=e;y=f}
    else if(cmd==='h'){x+=num();sp.lineTo(...P(x,y));lx=x;ly=y}
    else if(cmd==='Z'||cmd==='z'){}
    else i++}
  let sh=sp.toShapes(true);if(!sh.some(s=>s.holes.length))sh=sp.toShapes(false);return sh}
function gBadge(h,mat,depth=.02){const g=new T.ExtrudeGeometry(gShapes(),{depth,bevelEnabled:true,bevelThickness:.006,bevelSize:.004,bevelSegments:3,curveSegments:24});g.scale(h,h,1);return new T.Mesh(g,mat)}
function gTexture(size,bg,ink,glow){const c=cnv(size,size),g=c.getContext('2d');
  const rg=g.createRadialGradient(size*.4,size*.35,size*.05,size/2,size/2,size*.55);rg.addColorStop(0,bg[0]);rg.addColorStop(1,bg[1]);g.fillStyle=rg;g.fillRect(0,0,size,size);
  for(let a=0;a<360;a+=1.5){g.strokeStyle=`rgba(255,255,255,${.03+.03*Math.sin(a*.7)})`;g.beginPath();g.moveTo(size/2,size/2);g.lineTo(size/2+Math.cos(a)*size,size/2+Math.sin(a)*size);g.stroke()}
  const s=size*.62/289;g.save();g.translate(size/2-86*s,size/2-144*s);g.scale(s,s);if(glow){g.shadowColor=glow;g.shadowBlur=30}g.fillStyle=ink;g.fill(new Path2D(G_PATH));g.restore();return tex(c,{srgb:true})}

/* ---------- procedural texture helpers ---------- */
const cnv=(w,h)=>{const c=document.createElement('canvas');c.width=w;c.height=h;return c};
function vnoise(seed,period=256){const rnd=mulberry(seed),P=new Float32Array(period*period);for(let i=0;i<P.length;i++)P[i]=rnd();const m=period-1;
  const at=(x,y)=>P[((y&m)*period)+(x&m)];
  return (x,y)=>{const xi=Math.floor(x),yi=Math.floor(y),xf=x-xi,yf=y-yi,u=xf*xf*(3-2*xf),v=yf*yf*(3-2*yf),a=at(xi,yi),b=at(xi+1,yi),c=at(xi,yi+1),d=at(xi+1,yi+1);return a+(b-a)*u+(c-a)*v+(a-b-c+d)*u*v}}
const fbm=(n,x,y,o=5)=>{let s=0,a=.5,f=1;for(let i=0;i<o;i++){s+=a*n(x*f,y*f);f*=2;a*=.5}return s/(1-Math.pow(.5,o))};
const sm=(a,b,x)=>{const t=Math.min(1,Math.max(0,(x-a)/(b-a)));return t*t*(3-2*t)};
function normalFromHeight(h,w,hh,k,wrap){const c=cnv(w,hh),g=c.getContext('2d'),img=g.createImageData(w,hh),d=img.data;
  const at=(x,y)=>{if(wrap){x=(x+w)%w;y=(y+hh)%hh}else{x=x<0?0:x>=w?w-1:x;y=y<0?0:y>=hh?hh-1:y}return h[y*w+x]};
  for(let y=0;y<hh;y++)for(let x=0;x<w;x++){const nx=-(at(x+1,y)-at(x-1,y))*k,ny=(at(x,y+1)-at(x,y-1))*k,l=Math.hypot(nx,ny,1),i=(y*w+x)*4;
    d[i]=(nx/l*.5+.5)*255;d[i+1]=(ny/l*.5+.5)*255;d[i+2]=(1/l*.5+.5)*255;d[i+3]=255}
  g.putImageData(img,0,0);return c}
function grayCanvas(h,w,hh,f=v=>v){const c=cnv(w,hh),g=c.getContext('2d'),img=g.createImageData(w,hh),d=img.data;
  for(let i=0;i<w*hh;i++){const v=Math.max(0,Math.min(255,f(h[i])*255));d[i*4]=d[i*4+1]=d[i*4+2]=v;d[i*4+3]=255}g.putImageData(img,0,0);return c}
function tex(c,{srgb=false,rep=null}={}){const t=new T.CanvasTexture(c);if(srgb)t.colorSpace=T.SRGBColorSpace;t.anisotropy=8;
  if(rep){t.wrapS=t.wrapT=T.RepeatWrapping;t.repeat.set(rep[0],rep[1])}return t}

/* tolex / leather grain (tileable) */
function tolexMaps(seed,kind){const k='tolex'+seed+kind;if(cache[k])return cache[k];const N=512,n=vnoise(seed,64),n2=vnoise(seed+1,128),h=new Float32Array(N*N);
  for(let y=0;y<N;y++)for(let x=0;x<N;x++){const u=x/N*64,v=y/N*64;let a;
    if(kind==='croc'){const g=Math.abs(n(u*.5,v*.5)-.5)*2;a=sm(.05,.25,g)*.7+fbm(n2,u*2,v*2,3)*.3}
    else if(kind==='weave'){a=(Math.sin(x/N*Math.PI*2*96)*Math.sin(y/N*Math.PI*2*96)*.5+.5)*.55+fbm(n2,u,v,3)*.45}
    else{const r=1-Math.abs(fbm(n,u,v,4)-.5)*2;a=Math.pow(r,3)*.6+fbm(n2,u*2,v*2,3)*.4}
    h[y*N+x]=a}
  return cache[k]={normal:tex(normalFromHeight(h,N,N,kind==='weave'?2.5:4,true),{rep:[6,3]}),rough:tex(grayCanvas(h,N,N,v=>.55+v*.35),{rep:[6,3]})}}
/* brushed metal streaks */
function brushedMaps(seed){const k='br'+seed;if(cache[k])return cache[k];const w=1024,hh=64,n=vnoise(seed,256),h=new Float32Array(w*hh);
  for(let y=0;y<hh;y++){const row=fbm(n,0,y*3.1,4);for(let x=0;x<w;x++)h[y*w+x]=row*.7+fbm(n,x*.004,y*3.1+50,3)*.3}
  return cache[k]={rough:tex(grayCanvas(h,w,hh,v=>.22+v*.3),{rep:[1,6]}),normal:tex(normalFromHeight(h,w,hh,1.2,true),{rep:[1,6]})}}

/* ---------- themed relief panels: height, albedo, emissive ---------- */
const RW=1024, RHp=Math.round(1024*RH/IW);
function voronoi(seed,cx,cy){const rnd=mulberry(seed),pts=[];for(let j=-1;j<=cy;j++)for(let i=-1;i<=cx;i++)pts.push([(i+.15+rnd()*.7)/cx,(j+.15+rnd()*.7)/cy,rnd()]);
  return (u,v)=>{let d1=9,d2=9,id=0;for(const p of pts){const dx=(u-p[0])*cx,dy=(v-p[1])*cy,d=dx*dx+dy*dy;if(d<d1){d2=d1;d1=d;id=p[2]}else if(d<d2)d2=d}return [Math.sqrt(d1),Math.sqrt(d2),id]}}
function gMask(w,h,scale,blur=1.5){const c=cnv(w,h),g=c.getContext('2d');g.fillStyle='#000';g.fillRect(0,0,w,h);
  const p=new Path2D(G_PATH);
  const s=h*scale/289;g.save();g.translate(w/2-86*s,h/2-144*s);g.scale(s,s);g.filter=`blur(${blur}px)`;g.fillStyle='#fff';g.fill(p);g.restore();
  const d=g.getImageData(0,0,w,h).data,m=new Float32Array(w*h);for(let i=0;i<w*h;i++)m[i]=d[i*4]/255;return m}
function relief(key){if(cache['rel'+key])return cache['rel'+key];const th=THEMES[key],kind=th.rel||key;
  const w=RW,h=RHp,hgt=new Float32Array(w*h),alb=cnv(w,h),em=cnv(w,h),ga=alb.getContext('2d'),ge=em.getContext('2d'),A=ga.createImageData(w,h),E=ge.createImageData(w,h),a=A.data,e=E.data;
  const put=(i,ar,ag,ab,er,eg,eb)=>{a[i*4]=ar;a[i*4+1]=ag;a[i*4+2]=ab;a[i*4+3]=255;e[i*4]=er;e[i*4+1]=eg;e[i*4+2]=eb;e[i*4+3]=255};
  const n=vnoise(key.length*7+3,256);
  // the sharp 3D "g" sits in front of every relief; here only its soft backlight (blurred, dilated mask)
  if(kind==='haunt'){ // liquid ghost fog, one big soft violet glow spread across the panel
    const halo=gMask(w,h,.9,26);
    for(let y=0;y<h;y++)for(let x=0;x<w;x++){const u=x/w*3.6,v=y/h*3.96,i=y*w+x;
      const q=fbm(n,u,v,4),r=fbm(n,u+3.2*q+1.7,v+3.2*q+9.2,5);const hv=Math.pow(sm(.22,.88,r),1.2);hgt[i]=hv;
      const cx=(x/w-.5)*w/h*.62,cy=(y/h-.5)*1.05,dc=Math.hypot(cx,cy),big=Math.exp(-dc*dc/.32);
      const ao=.45+.55*hv;const glowAmt=(Math.pow(1-hv,2.2)*.75+.25)*(.15+1.1*big)+halo[i]*.9;
      put(i,(70+40*big)*ao,(64+26*big)*ao,(84+60*big)*ao,150*glowAmt,46*glowAmt,255*glowAmt)}}
  else if(kind==='grave'){ // broken grave slabs, ember light in the cracks and behind the iron g
    const vo=voronoi(11,13,4),halo=gMask(w,h,.66,14);
    for(let y=0;y<h;y++)for(let x=0;x<w;x++){const u=x/w,v=y/h,i=y*w+x,[d1,d2,id]=vo(u,v),edge=d2-d1;
      const slab=sm(.0,.12,edge),grit=fbm(n,u*60,v*20,4);const cen=Math.exp(-Math.pow((u-.5)*w/h,2)/.16-Math.pow(v-.5,2)/.18);
      const hv=(slab*(1-cen)+cen*.9)*(.72+id*.18*(1-cen))+grit*.12-(.08*(u-.5)*id*(1-cen));
      hgt[i]=Math.max(0,hv);const rust=sm(.55,.8,fbm(n,u*8+id*5,v*3,4));const ao=.3+.7*Math.max(slab,cen);
      const c=(.22+grit*.25)*ao;const crack=Math.exp(-edge*edge/.0009)*(.55+.45*fbm(n,u*30,v*10,3))*(1-cen)+halo[i]*1.2;
      put(i,(c*255*.9+rust*70*ao),(c*255*.85+rust*30*ao),c*255*.8,255*crack,95*crack,20*crack)}}
  else if(kind==='spectre'){ // "Astral": smoked glass, nebula and stars behind it, the g drawn as a constellation
    const stars=mulberry(77);
    for(let y=0;y<h;y++)for(let x=0;x<w;x++){const u=x/w,v=y/h,i=y*w+x;hgt[i]=.04*fbm(n,u*40,v*12,2);
      const q=fbm(n,u*3,v*1.2,5),r=fbm(n,u*2.2+q*2.4,v*1.6+q*1.8+4,5);const neb=Math.pow(sm(.38,.9,r),1.5);const mix=fbm(n,u*1.5+7,v*1.5,3);
      const dust=Math.pow(sm(.5,.9,fbm(n,u*9,v*3,4)),3)*.5;
      put(i,10,12,22,(40+180*(1-mix))*neb+dust*40,(120+110*mix)*neb+dust*60,(200+55*mix)*neb+dust*90)}
    ga.putImageData(A,0,0);ge.putImageData(E,0,0);
    for(let k=0;k<900;k++){const x=stars()*w,y=stars()*h,r=Math.pow(stars(),6)*2.2+.4;ge.fillStyle=`rgba(230,240,255,${.35+stars()*.65})`;ge.beginPath();ge.arc(x,y,r,0,7);ge.fill()}
    const sh=gShapes()[0],pts=sh.getSpacedPoints(64),hole=sh.holes[0]?sh.holes[0].getSpacedPoints(18):[],gh=.72*h,P=q=>[w/2+q.x*gh,h/2-q.y*gh];
    const node=(q,big)=>{const [x,y]=P(q);const rg=ge.createRadialGradient(x,y,0,x,y,big?14:9);rg.addColorStop(0,'rgba(255,255,255,1)');rg.addColorStop(.25,'rgba(190,240,255,.9)');rg.addColorStop(1,'rgba(120,200,255,0)');ge.fillStyle=rg;ge.beginPath();ge.arc(x,y,big?14:9,0,7);ge.fill()};
    for(const ring of [pts,hole]){if(!ring.length)continue;ge.strokeStyle='rgba(170,230,255,.55)';ge.lineWidth=1.6;ge.beginPath();ring.forEach((q,k)=>{const [x,y]=P(q);k?ge.lineTo(x,y):ge.moveTo(x,y)});ge.closePath();ge.stroke();
      ring.forEach((q,k)=>{if(k%3===0)node(q,k%9===0)})}
    return finishRelief(key,th,alb,em,hgt,4)}
  else { // veil: one wide gathered drape across the whole front, warm light from below
    const halo=gMask(w,h,.7,22);
    for(let y=0;y<h;y++)for(let x=0;x<w;x++){const u=x/w,v=y/h,i=y*w+x;
      const wob=fbm(n,u*3,v*2,3)*.9+Math.sin(v*2.2+u*4)*.25,freq=30-12*Math.pow(v,.8);
      const f=Math.cos(((u-.5)*freq+wob)*Math.PI)*.5+.5,amp=.3+.7*Math.pow(v,.65),sag=Math.exp(-Math.pow((v-.06)/.07,2))*.22;
      const weave=(Math.sin(x*1.9)*Math.sin(y*1.9))*.012+fbm(n,u*300,v*100,2)*.03;
      const hv=Math.pow(f,1.35)*amp*.85+sag+weave+.1;hgt[i]=hv;const ao=.5+.5*Math.min(1,hv);
      const lit=Math.pow(Math.max(0,v-.45)/.55,2)*Math.pow(1-f,1.5)*.8+halo[i]*.25;
      put(i,240*ao,230*ao,208*ao,255*lit*.6,180*lit*.6,95*lit*.6)}}
  ga.putImageData(A,0,0);ge.putImageData(E,0,0);
  return finishRelief(key,th,alb,em,hgt,kind==='veil'?7:9)}
// themes that borrow another relief: tint the albedo and turn the glow map into pure intensity, so the theme's emissive colour decides the light
function finishRelief(key,th,alb,em,hgt,ns){const w=alb.width,h=alb.height;
  if(th.rel){const ga=alb.getContext('2d'),ge=em.getContext('2d'),A=ga.getImageData(0,0,w,h),E=ge.getImageData(0,0,w,h),a=A.data,e=E.data,t=th.relTint||[1,1,1];
    for(let i=0;i<a.length;i+=4){a[i]*=t[0];a[i+1]*=t[1];a[i+2]*=t[2];const l=Math.max(e[i],e[i+1],e[i+2])*(th.relGlow||1);e[i]=e[i+1]=e[i+2]=Math.min(255,l)}
    ga.putImageData(A,0,0);ge.putImageData(E,0,0)}
  return cache['rel'+key]={map:tex(alb,{srgb:true}),emissive:tex(em,{srgb:true}),normal:tex(normalFromHeight(hgt,w,h,ns,false)),disp:tex(grayCanvas(hgt,w,h))}}

/* ---------- faceplate silkscreen ---------- */
function plateCanvas(key){const th=THEMES[key],w=4096,h=Math.round(4096*PH/IW),c=cnv(w,h),g=c.getContext('2d');
  const X=x=>(x+IW/2)/IW*w, Y=y=>(1-(y-(-H/2+FR))/PH)*h, px=w/IW;
  g.fillStyle=th.plate;g.fillRect(0,0,w,h);
  const gr=g.createLinearGradient(0,0,0,h);gr.addColorStop(0,'rgba(255,255,255,.10)');gr.addColorStop(.5,'rgba(255,255,255,0)');gr.addColorStop(1,'rgba(0,0,0,.18)');g.fillStyle=gr;g.fillRect(0,0,w,h);
  if(key==='grave'){g.save();g.globalAlpha=.9;for(let x=-h;x<w;x+=92){g.fillStyle='#e8b21c';g.beginPath();g.moveTo(x,h);g.lineTo(x+46,h);g.lineTo(x+46+28,h-28);g.lineTo(x+28,h-28);g.fill()}g.restore();
    const n=vnoise(9,256);const img=g.getImageData(0,0,w,h),d=img.data;for(let y=0;y<h;y++)for(let x=0;x<w;x++){const r=sm(.55,.75,fbm(n,x*.01,y*.03,4)),i=(y*w+x)*4;d[i]+=r*60;d[i+1]+=r*18;d[i+2]-=r*10}g.putImageData(img,0,0)}
  g.fillStyle=th.ink;g.strokeStyle=th.ink;g.textAlign='center';g.textBaseline='middle';
  const LF="'Archivo',sans-serif";if('fontStretch' in g)g.fontStretch='expanded';
  g.font=`800 ${Math.round(px*.036)}px ${LF}`;if('letterSpacing' in g)g.letterSpacing=`${Math.round(px*.004)}px`;
  KX.forEach((x,i)=>{g.fillText(LABELS[i],X(x),Y(KY+.17));
    for(let t=0;t<=11;t++){const a=(-150+300*t/11)*Math.PI/180,r=.112*px,cx=X(x),cy=Y(KY);g.beginPath();g.arc(cx+Math.sin(a)*r,cy-Math.cos(a)*r,t===0||t===11?px*.0075:px*.0045,0,7);g.fill()}});
  g.font=`800 ${Math.round(px*.026)}px ${LF}`;g.fillText('TIGHT',X(TX.ampTight),Y(KY+.17));g.fillText('BRIGHT',X(TX.ampBright),Y(KY+.17));g.fillText('INPUT',X(JX),Y(KY+.17));
  if('fontStretch' in g)g.fontStretch=th.logoStretch||'normal';
  g.font=`${th.logoWeight||400} ${Math.round(px*th.logoSize)}px ${th.logoFont}`;if('letterSpacing' in g)g.letterSpacing=th.logoLs||'0px';
  if(th.logoGlow){g.shadowColor=th.logoGlow;g.shadowBlur=px*.03}g.fillStyle=th.logoInk||th.ink;g.fillText(th.logo,X(LOGOX),Y(KY+.02));g.shadowBlur=0;
  if('fontStretch' in g)g.fontStretch='expanded';g.font=`700 ${Math.round(px*.019)}px ${LF}`;g.globalAlpha=.8;g.fillStyle=th.ink;g.fillText(th.sub,X(LOGOX),Y(KY-.13));g.globalAlpha=1;

  return tex(c,{srgb:true})}

/* ---------- themes (look of each amp) ---------- */
const THEMES={
  haunt:{tolex:'#17161b',tolexKind:'leather',plate:'#2a2b30',ink:'#e9e6df',metal:.6,plateRough:.38,knobCap:'#0d0d0f',knobSkirt:'#b9bcc3',capMetal:.2,capRough:.25,
    logo:'HAUNT',logoFont:"'Michroma',sans-serif",logoSize:.068,logoLs:'6px',sub:'RHYTHM · 4 STAGES',labelFont:"'Michroma',sans-serif",relRough:.55,relMetal:.1,emissive:'#b06bff',glowBase:.35,glowGain:1.6,emblem:{h:.8,color:'#1b1b20',metal:1,rough:.18,clear:1,glow:'#9d5cff'},jewel:'#ff2d2d',handle:'#141416',piping:'#c9ccd2',badge:'#ffffff',insertBg:['#d9dce2','#6d717a'],insertInk:'#121214'},
  grave:{tolex:'#1d1b18',tolexKind:'leather',plate:'#5a554d',ink:'#f1e6cf',metal:.5,plateRough:.6,knobCap:'#2a1d14',knobSkirt:'#c47c45',capMetal:.6,capRough:.35,
    logo:'GRAVE 666',logoFont:"'Black Ops One',Impact,sans-serif",logoSize:.074,logoInk:'#f08a3a',logoGlow:'rgba(255,120,40,.6)',sub:"ANDI'S BREAKDOWN-BUNKER",labelFont:"'Michroma',sans-serif",relRough:.85,relMetal:.05,emissive:'#ff6a1a',glowBase:.45,glowGain:2.2,emblem:{h:.68,color:'#5a4330',metal:.85,rough:.5,clear:0,glow:'#ff7a20',iron:1},jewel:'#ff9a1a',handle:'#2a2622',piping:'#b8743f',badge:'#e0a070',insertBg:['#d79a62','#5a3218'],insertInk:'#1a0d05',insertGlow:'rgba(255,120,40,.6)'},
  spectre:{tolex:'#0e1420',tolexKind:'croc',plate:'#a9b4c6',ink:'#0c1320',metal:.6,plateRough:.28,knobCap:'#0b0f18',knobSkirt:'#dfe6f2',capMetal:.3,capRough:.18,
    logo:'SPECTRE',logoFont:"'Michroma',sans-serif",logoSize:.062,logoLs:'8px',logoInk:'#f2fbff',logoGlow:'rgba(110,210,255,1)',sub:'SILVAN · ASTRAL LEAD',relRough:.05,relMetal:0,emissive:'#ffffff',glowBase:.8,glowGain:.9,jewel:'#5ff2ff',handle:'#121a28',piping:'#9fd8ff',badge:'#e6f6ff',insertBg:['#1a2a44','#05080f'],insertInk:'#bfeaff',insertGlow:'rgba(110,210,255,1)',
    emblem:null},
  veil:{tolex:'#d8ccb0',tolexKind:'weave',plate:'#d1ad62',ink:'#2a2112',metal:.45,plateRough:.32,knobCap:'#f6efdc',knobSkirt:'#d8c79e',capMetal:0,capRough:.35,
    logo:'Veil',logoFont:"'Archivo',sans-serif",logoWeight:'italic 800',logoStretch:'normal',logoSize:.1,sub:'CLEAN · CLASS A',labelFont:"'Michroma',sans-serif",relRough:.9,relMetal:0,emissive:'#ffb060',glowBase:.25,glowGain:1.0,emblem:{h:.7,color:'#0c0c0e',metal:1,rough:.12,clear:1,glow:'#ffb060'},jewel:'#46ff9b',handle:'#8a6a3a',piping:'#6b5430',badge:'#e3c27a',insertBg:['#fbf6e8','#cfc3a4'],insertInk:'#7a5a22'},
  // Legends
  ravager:{rel:'grave',relTint:[.42,.38,.38],relGlow:.9,tolex:'#0e0e0f',tolexKind:'leather',plate:'#18181a',ink:'#e4e4e4',metal:.55,plateRough:.42,knobCap:'#0a0a0b',knobSkirt:'#c3c6cc',capMetal:.3,capRough:.3,
    logo:'RAVAGER',logoFont:"'Black Ops One',Impact,sans-serif",logoSize:.078,logoLs:'4px',logoInk:'#ff3b2e',logoGlow:'rgba(255,40,20,.75)',sub:'5150-ART · LEAD · 6L6',relRough:.8,relMetal:.08,emissive:'#ff2a14',glowBase:.4,glowGain:2,
    emblem:{h:.68,color:'#2a1414',metal:.9,rough:.4,clear:0,glow:'#ff2a14',iron:1},jewel:'#ff2020',handle:'#111',piping:'#8a1c16',badge:'#d0d0d0',insertBg:['#2a2a2e','#050506'],insertInk:'#ff4a3a',insertGlow:'rgba(255,40,20,.7)'},
  reckoner:{rel:'haunt',relTint:[.55,.6,.68],relGlow:.75,tolex:'#121214',tolexKind:'leather',plate:'#9a9da3',ink:'#0d0e10',metal:.95,plateRough:.3,knobCap:'#0c0c0d',knobSkirt:'#d9dce2',capMetal:.4,capRough:.2,
    logo:'RECKONER',logoFont:"'Michroma',sans-serif",logoSize:.06,logoLs:'7px',logoInk:'#0d0e10',sub:'RECTO-ART · MODERN · 6L6',relRough:.45,relMetal:.4,emissive:'#a8d0ff',glowBase:.3,glowGain:1.4,
    emblem:{h:.78,color:'#d8dbe0',metal:1,rough:.12,clear:1,glow:'#a8d0ff'},jewel:'#ff3a2a',handle:'#1a1a1c',piping:'#d9dce2',badge:'#ffffff',insertBg:['#e6e8ec','#7b8088'],insertInk:'#101114'},
  puppeteer:{rel:'veil',relTint:[.34,.24,.15],relGlow:.8,tolex:'#100e0c',tolexKind:'croc',plate:'#151312',ink:'#eadfc6',metal:.4,plateRough:.5,knobCap:'#0b0a09',knobSkirt:'#cbb98e',capMetal:.2,capRough:.3,ptr:0xeadfc6,
    logo:'Puppeteer',logoFont:"'Archivo',sans-serif",logoWeight:'italic 800',logoStretch:'normal',logoSize:.078,logoInk:'#e2c27a',sub:'MK IIC+-ART · PRE-EQ LEAD',relRough:.9,relMetal:0,emissive:'#ffc46a',glowBase:.3,glowGain:1.3,
    emblem:{h:.7,color:'#c9a04a',metal:1,rough:.22,clear:1,glow:'#ffc46a'},jewel:'#ff2020',handle:'#2a1e12',piping:'#c9a04a',badge:'#e2c27a',insertBg:['#2a241c','#070605'],insertInk:'#e2c27a'},
  crown:{rel:'veil',relTint:[.16,.15,.14],relGlow:.7,tolex:'#121212',tolexKind:'leather',plate:'#c9a955',ink:'#151208',metal:.75,plateRough:.3,knobCap:'#0d0d0d',knobSkirt:'#d9c58a',capMetal:.1,capRough:.35,ptr:0xf2efe6,
    logo:'OLD CROWN',logoFont:"'Black Ops One',Impact,sans-serif",logoSize:.066,logoLs:'3px',logoInk:'#151208',sub:'JCM800-ART · EL34 · 100 W',relRough:.95,relMetal:0,emissive:'#ffb050',glowBase:.25,glowGain:1.2,
    emblem:{h:.7,color:'#d4b25c',metal:1,rough:.18,clear:1,glow:'#ffb050'},jewel:'#ff2d2d',handle:'#111',piping:'#efe6c8',badge:'#e8d08a',insertBg:['#e8d08a','#7a6224'],insertInk:'#141007'},
  hammer:{rel:'grave',relTint:[.3,.36,.46],relGlow:.95,spots:1,spotColor:0x8cc4ff,tolex:'#0b0b0d',tolexKind:'leather',plate:'#1f2226',ink:'#d3dae4',metal:.7,plateRough:.36,knobCap:'#0b0c0e',knobSkirt:'#aeb6c2',capMetal:.4,capRough:.25,
    logo:'VALI HAMMER',logoFont:"'Black Ops One',Impact,sans-serif",logoSize:.058,logoLs:'3px',logoInk:'#6fb6ff',logoGlow:'rgba(60,150,255,.8)',sub:'VH4-ART · KANAL 3 · KT77',relRough:.8,relMetal:.1,emissive:'#3a8cff',glowBase:.4,glowGain:2,
    emblem:{h:.68,color:'#1c2430',metal:.9,rough:.4,clear:0,glow:'#3a8cff',iron:1},jewel:'#3aa0ff',handle:'#15171a',piping:'#3a6ea8',badge:'#c8d4e4',insertBg:['#26303e','#05070a'],insertInk:'#6fb6ff',insertGlow:'rgba(60,150,255,.7)'}
};

/* ---------- geometry ---------- */
function roundedRectShape(w,h,r){const s=new T.Shape(),x=-w/2,y=-h/2;s.moveTo(x+r,y);s.lineTo(x+w-r,y);s.quadraticCurveTo(x+w,y,x+w,y+r);s.lineTo(x+w,y+h-r);s.quadraticCurveTo(x+w,y+h,x+w-r,y+h);
  s.lineTo(x+r,y+h);s.quadraticCurveTo(x,y+h,x,y+h-r);s.lineTo(x,y+r);s.quadraticCurveTo(x,y,x+r,y);return s}
function build(){
  const g=new T.Group();const parts={};
  // cabinet shell = rounded frame (outer minus inner) extruded in depth
  const outer=roundedRectShape(W,H,.12),inner=roundedRectShape(IW,H-2*FR,.04);outer.holes.push(inner);
  const shellGeo=new T.ExtrudeGeometry(outer,{depth:D-.12,bevelEnabled:true,bevelThickness:.06,bevelSize:.05,bevelSegments:6,curveSegments:24});shellGeo.translate(0,0,-D/2+.06);
  parts.tolex=new T.MeshStandardMaterial({roughness:.8,metalness:0});
  g.add(new T.Mesh(shellGeo,parts.tolex));
  const back=new T.Mesh(new T.PlaneGeometry(IW,H-2*FR),new T.MeshStandardMaterial({color:0x050505,roughness:1}));back.position.z=-D/2+.1;g.add(back);
  // relief panel (upper front)
  parts.relief=new T.MeshStandardMaterial({displacementScale:.075,emissive:0xffffff});
  const rel=new T.Mesh(new T.PlaneGeometry(IW,RH,320,Math.round(320*RH/IW)),parts.relief);rel.position.set(0,H/2-FR-RH/2,PZ-.1);g.add(rel);
  // thin trim strip between relief and plate
  const trim=new T.Mesh(new T.BoxGeometry(IW,.035,.06),parts.trim=new T.MeshStandardMaterial({color:0x111111,metalness:1,roughness:.25}));trim.position.set(0,-H/2+FR+PH,PZ-.01);g.add(trim);
  // faceplate
  parts.plate=new T.MeshStandardMaterial({metalness:.8,roughness:.35});
  const plate=new T.Mesh(new T.BoxGeometry(IW,PH,.03),[parts.trim,parts.trim,parts.trim,parts.trim,parts.plate,parts.trim]);plate.position.set(0,-H/2+FR+PH/2,PZ-.015);g.add(plate);
  // knobs: knurled skirt + domed cap + pointer
  const prof=[[0,0],[.088,0],[.09,.006],[.09,.02],[.084,.026],[.07,.03],[.067,.075],[.062,.088],[.045,.096],[0,.099]].map(p=>new T.Vector2(p[0],p[1]));
  const capGeo=new T.LatheGeometry(prof.slice(4),64);capGeo.rotateX(Math.PI/2);
  const skirtGeo=new T.CylinderGeometry(.09,.092,.026,96,1);skirtGeo.rotateX(Math.PI/2);skirtGeo.translate(0,0,.013);
  const knurl=cnv(512,8),kg=knurl.getContext('2d');for(let i=0;i<512;i+=8){kg.fillStyle='#fff';kg.fillRect(i,0,4,8);kg.fillStyle='#000';kg.fillRect(i+4,0,4,8)}
  const knurlTex=tex(normalFromHeight((()=>{const d=kg.getImageData(0,0,512,8).data,h=new Float32Array(512*8);for(let i=0;i<h.length;i++)h[i]=d[i*4]/255;return h})(),512,8,1.5,true));
  parts.cap=new T.MeshPhysicalMaterial({clearcoat:1,clearcoatRoughness:.15});parts.skirt=new T.MeshStandardMaterial({metalness:1,roughness:.22,normalMap:knurlTex,normalScale:new T.Vector2(.8,.8)});
  const ptrMat=parts.ptr=new T.MeshStandardMaterial({color:0xf2efe6,roughness:.4,emissive:0x222222});
  parts.knobs={};const insertGeo=new T.CircleGeometry(.047,48);parts.insert=new T.MeshStandardMaterial({metalness:.9,roughness:.3});
  KX.forEach((x,i)=>{const k=new T.Group();k.add(new T.Mesh(skirtGeo,parts.skirt));const cap=new T.Mesh(capGeo,parts.cap);k.add(cap);
    const p=new T.Mesh(new T.BoxGeometry(.012,.03,.008),ptrMat);p.position.set(0,.074,.031);k.add(p);const ins=new T.Mesh(insertGeo,parts.insert);ins.position.z=.1;k.add(ins);
    k.position.set(x,KY,PZ);g.add(k);parts.knobs[KEYS[i]]=k});
  // bat toggles
  parts.toggles={};const nutGeo=new T.CylinderGeometry(.028,.028,.02,6);nutGeo.rotateX(Math.PI/2);const batGeo=new T.CylinderGeometry(.008,.012,.085,24);batGeo.translate(0,.0425,0);
  const chrome=parts.chrome=new T.MeshStandardMaterial({color:0xffffff,metalness:1,roughness:.12});
  for(const id of ['ampTight','ampBright']){const t=new T.Group();const nut=new T.Mesh(nutGeo,chrome);nut.position.z=.01;t.add(nut);const bat=new T.Mesh(batGeo,chrome);bat.position.z=.02;t.add(bat);t.userData.bat=bat;t.position.set(TX[id],KY,PZ);g.add(t);parts.toggles[id]=t}
  // input jack + cable hanging out of frame
  const jack=new T.Mesh(new T.TorusGeometry(.04,.014,16,48),chrome);jack.position.set(JX,KY,PZ+.012);g.add(jack);
  const plug=new T.Mesh(new T.CylinderGeometry(.032,.032,.09,32),new T.MeshStandardMaterial({color:0x1a1a1a,roughness:.5}));plug.rotation.x=Math.PI/2;plug.position.set(JX,KY,PZ+.06);g.add(plug);
  const curve=new T.CatmullRomCurve3([new T.Vector3(JX,KY,PZ+.1),new T.Vector3(JX-.05,KY-.12,PZ+.35),new T.Vector3(JX-.25,-H/2-.25,PZ+.45),new T.Vector3(JX-.9,-H/2-.42,PZ+.4),new T.Vector3(-W/2-1.4,-H/2-.46,PZ+.3)]);
  g.add(new T.Mesh(new T.TubeGeometry(curve,80,.022,12),new T.MeshStandardMaterial({color:0x101010,roughness:.45})));
  // jewel lamp
  parts.jewel=new T.MeshPhysicalMaterial({roughness:.05,transmission:0,clearcoat:1,emissiveIntensity:2});
  const jw=new T.Mesh(new T.SphereGeometry(.04,32,16),parts.jewel);jw.position.set(JEWX,KY,PZ+.012);jw.scale.z=.6;g.add(jw);
  const bezel=new T.Mesh(new T.TorusGeometry(.046,.01,12,48),chrome);bezel.position.set(JEWX,KY,PZ+.006);g.add(bezel);
  parts.jewelLight=new T.PointLight(0xff0000,0,.8,2);parts.jewelLight.position.set(JEWX,KY,PZ+.15);g.add(parts.jewelLight);
  // handle with end caps
  const hc=new T.CatmullRomCurve3([new T.Vector3(-.75,H/2+.02,0),new T.Vector3(-.62,H/2+.2,0),new T.Vector3(.62,H/2+.2,0),new T.Vector3(.75,H/2+.02,0)]);
  parts.handle=new T.MeshStandardMaterial({roughness:.6});g.add(new T.Mesh(new T.TubeGeometry(hc,64,.04,16),parts.handle));
  for(const sx of [-1,1]){const cp=new T.Mesh(new T.BoxGeometry(.24,.05,.16),chrome);cp.position.set(sx*.74,H/2+.02,0);g.add(cp)}
  // chrome corner protectors on the front face
  {const L=new T.Shape(),a=.26,t=.085,r=.07;L.moveTo(0,0);L.lineTo(a,0);L.lineTo(a,t);L.lineTo(t,t);L.lineTo(t,a);L.lineTo(0,a);L.lineTo(0,0);
   const cg=new T.ExtrudeGeometry(L,{depth:.02,bevelEnabled:true,bevelThickness:.012,bevelSize:.012,bevelSegments:4});
   for(const [sx,sy] of [[-1,1],[1,1],[-1,-1],[1,-1]]){const c=new T.Mesh(cg,chrome);c.position.set(sx*(W/2-.015),sy*(H/2-.015),D/2+.005);c.scale.set(-sx,-sy,1);g.add(c)}}
  // brand badge: chrome g on the upper-left frame
  parts.badge=new T.MeshStandardMaterial({color:0xffffff,metalness:1,roughness:.15});
  {const b=gBadge(.115,parts.badge,.03);b.position.set(-W/2+.34,H/2-FR/2+.005,D/2+.03);g.add(b)}
  // piping around the front opening
  {const pts=roundedRectShape(IW+.03,H-2*FR+.03,.05).getSpacedPoints(400).slice(0,-1).map(q=>new T.Vector3(q.x,q.y,D/2-.004));
   parts.piping=new T.MeshStandardMaterial({metalness:.3,roughness:.35});g.add(new T.Mesh(new T.TubeGeometry(new T.CatmullRomCurve3(pts,true),400,.011,8,true),parts.piping))}
  // faceplate screws
  {const sg=new T.CylinderGeometry(.016,.016,.01,20);sg.rotateX(Math.PI/2);for(const [sx,sy] of [[-1,1],[1,1],[-1,-1],[1,-1]]){const sc=new T.Mesh(sg,chrome);sc.position.set(sx*(IW/2-.05),-H/2+FR+PH/2+sy*(PH/2-.05),PZ+.004);g.add(sc)}}
  // feet
  for(const sx of [-1,1]){const f=new T.Mesh(new T.CylinderGeometry(.1,.12,.12,32),new T.MeshStandardMaterial({color:0x0a0a0a,roughness:.6}));f.position.set(sx*(W/2-.45),-H/2-.06,.2);g.add(f)}
  // speaker cabinet top below
  parts.cabTolex=new T.MeshStandardMaterial({roughness:.85});
  const cab=new T.Mesh(new T.BoxGeometry(W+.6,.9,D+.15),parts.cabTolex);cab.position.set(0,-H/2-.12-.45,.0);g.add(cab);
  {const gc=cnv(1024,128),gg=gc.getContext('2d');gg.fillStyle='#202022';gg.fillRect(0,0,1024,128);gg.fillStyle='rgba(255,255,255,.07)';for(let x=0;x<1024;x+=4)gg.fillRect(x,0,1,128);for(let y=0;y<128;y+=4)gg.fillRect(0,y,1024,1);
   parts.grille=new T.MeshStandardMaterial({map:tex(gc,{srgb:true}),roughness:.95});const gr=new T.Mesh(new T.PlaneGeometry(W+.3,.7),parts.grille);gr.position.set(0,-H/2-.12-.5,(D+.15)/2+.002);g.add(gr);
   const cb=gBadge(.3,parts.badge,.03);cb.position.set(0,-H/2-.12-.42,(D+.15)/2+.01);g.add(cb)}
  const shadow=cnv(256,64),sg=shadow.getContext('2d'),rg=sg.createRadialGradient(128,32,4,128,32,128);rg.addColorStop(0,'rgba(0,0,0,.75)');rg.addColorStop(1,'rgba(0,0,0,0)');sg.fillStyle=rg;sg.fillRect(0,0,256,64);
  const sh=new T.Mesh(new T.PlaneGeometry(W*1.25,D*1.3),new T.MeshBasicMaterial({map:tex(shadow),transparent:true,depthWrite:false}));sh.rotation.x=-Math.PI/2;sh.position.set(0,-H/2-.115,.1);g.add(sh);
  // the sharp 3D Ghostlike g in front of the relief, with a backlight for the rim glow
  parts.emblemMat=new T.MeshPhysicalMaterial({metalness:1,roughness:.2,clearcoat:1,clearcoatRoughness:.1});
  parts.emblem=gBadge(1,parts.emblemMat,.09);parts.emblem.geometry.translate(0,0,0);parts.emblem.position.set(0,H/2-FR-RH/2,PZ-.06);g.add(parts.emblem);
  parts.emblemLight=new T.PointLight(0xffffff,0,1.6,2);parts.emblemLight.position.set(0,H/2-FR-RH/2,PZ-.02);g.add(parts.emblemLight);
  // GRAVE: three small spotlights in the lower frame shining up onto the faceplate
  parts.spots=new T.Group();g.add(parts.spots);
  {const hous=new T.CylinderGeometry(.035,.045,.05,24),lens=new T.CircleGeometry(.03,24);parts.lensMat=new T.MeshStandardMaterial({color:0x221100,emissive:0xffb060,emissiveIntensity:3});
   for(const x of [-1.05,0,1.05]){const gr2=new T.Group();const hh=new T.Mesh(hous,new T.MeshStandardMaterial({color:0x2a2622,metalness:.8,roughness:.4}));gr2.add(hh);
     const ln=new T.Mesh(lens,parts.lensMat);ln.rotation.x=-Math.PI/2;ln.position.y=.026;gr2.add(ln);gr2.position.set(x,-H/2+FR*.55,D/2+.03);parts.spots.add(gr2);
     const sp=new T.SpotLight(0xffb070,0,1.4,.75,.7,1.6);sp.position.set(x,-H/2+FR*.6,D/2+.06);sp.target.position.set(x,-H/2+FR+PH*.75,PZ);parts.spots.add(sp);parts.spots.add(sp.target)}}
  // light that spills out of the relief onto the faceplate
  parts.spill=new T.PointLight(0xff8040,0,2.2,2);parts.spill.position.set(0,-H/2+FR+PH+.05,PZ+.25);g.add(parts.spill);
  return {group:g,parts}}

function envMap(r=R){const s=new T.Scene();s.background=new T.Color(0x0b0b0c);
  const box=new T.Mesh(new T.BoxGeometry(30,30,30),new T.MeshBasicMaterial({color:0x101012,side:T.BackSide}));s.add(box);
  const panel=(w,h,c,i,pos,look)=>{const m=new T.Mesh(new T.PlaneGeometry(w,h),new T.MeshBasicMaterial({color:new T.Color(c).multiplyScalar(i)}));m.position.set(...pos);m.lookAt(...look);s.add(m)};
  panel(14,4,0xffffff,6,[0,12,6],[0,0,0]);          // big softbox above/front
  panel(3,12,0xdfe6ff,2.2,[-13,2,4],[0,0,0]);       // cool strip left
  panel(3,12,0xffe2c4,1.6,[13,2,2],[0,0,0]);        // warm strip right
  panel(20,6,0x3a3a40,1,[0,-12,0],[0,0,0]);         // floor bounce
  const pm=new T.PMREMGenerator(r);const rt=pm.fromScene(s,.04);pm.dispose();return rt.texture}

/* ---------- theme application ---------- */
function applyTheme(key){const th=THEMES[key],p=built.parts,tl=tolexMaps(key.length,th.tolexKind),br=brushedMaps(3),rl=relief(key);theme=key;
  Object.assign(p.tolex,{color:new T.Color(th.tolex),normalMap:tl.normal,roughnessMap:tl.rough,normalScale:new T.Vector2(.9,.9)});p.tolex.needsUpdate=true;
  Object.assign(p.cabTolex,{color:new T.Color(th.tolex).multiplyScalar(.6),normalMap:tl.normal,roughnessMap:tl.rough,normalScale:new T.Vector2(.5,.5)});p.cabTolex.needsUpdate=true;
  Object.assign(p.relief,{map:rl.map,normalMap:rl.normal,displacementMap:rl.disp,emissiveMap:rl.emissive,emissive:new T.Color(th.emissive),roughness:th.relRough,metalness:th.relMetal,normalScale:new T.Vector2(1.2,1.2)});
  p.relief.color=new T.Color(0xffffff);p.relief.needsUpdate=true;
  Object.assign(p.plate,{map:plateCanvas(key),metalness:th.metal,roughness:th.plateRough,roughnessMap:br.rough,normalMap:br.normal,normalScale:new T.Vector2(.25,.25)});p.plate.needsUpdate=true;
  Object.assign(p.cap,{color:new T.Color(th.knobCap),metalness:th.capMetal,roughness:th.capRough});Object.assign(p.skirt,{color:new T.Color(th.knobSkirt)});
  p.ptr.color=new T.Color(th.ptr??(key==='veil'?0x2a2112:key==='spectre'?0xefe9ff:0xf2efe6));
  p.jewel.color=new T.Color(th.jewel);p.jewel.emissive=new T.Color(th.jewel);p.jewelLight.color=new T.Color(th.jewel);
  const em=th.emblem;p.emblem.visible=!!em;if(em){p.emblem.scale.set(em.h*RH,em.h*RH,1);Object.assign(p.emblemMat,{color:new T.Color(em.color),metalness:em.metal,roughness:em.rough,clearcoat:em.clear});
    p.emblemMat.roughnessMap=em.iron?tolexMaps(99,'leather').rough:null;p.emblemMat.needsUpdate=true;p.emblemLight.color=new T.Color(em.glow)}
  p.spots.visible=key==='grave'||!!th.spots;p.spots.children.forEach(c=>{if(c.isSpotLight)c.color=new T.Color(th.spotColor??0xffb070)});
  p.handle.color=new T.Color(th.handle);p.piping.color=new T.Color(th.piping);p.badge.color=new T.Color(th.badge);
  p.insert.map=gTexture(256,th.insertBg,th.insertInk,th.insertGlow);p.insert.needsUpdate=true;p.spill.color=new T.Color(th.emissive);
  dirty=true}

/* ---------- interaction overlay ---------- */
const hits=[];
function makeOverlay(){
  KEYS.forEach((id,i)=>{const el=document.createElement('div');el.className='kn hit';el.dataset.p=id;el.dataset.l=LABELS[i];overlay.appendChild(el);mountKnob(el);hits.push([el,new T.Vector3(KX[i],KY,PZ+.05),.1])});
  for(const id of ['ampTight','ampBright']){const b=document.createElement('button');b.className='hitbtn';b.dataset.toggle=id;b.setAttribute('aria-label',id==='ampTight'?'Tight':'Bright');b.addEventListener('click',()=>set(id,!state[id]));overlay.appendChild(b);hits.push([b,new T.Vector3(TX[id],KY,PZ),.07])}
  const pw=document.createElement('button');pw.className='hitbtn';pw.dataset.toggle='power';pw.setAttribute('aria-label','Power / Standby');pw.title='Power / Standby';pw.addEventListener('click',()=>set('power',!state.power));overlay.appendChild(pw);hits.push([pw,new T.Vector3(JEWX,KY,PZ),.08]);
}
function placeHits(){const r=host.getBoundingClientRect();for(const [el,pos,rad] of hits){const v=pos.clone().project(cam),e=pos.clone().add(new T.Vector3(rad,0,0)).project(cam);
  const x=(v.x*.5+.5)*r.width,y=(-v.y*.5+.5)*r.height,pr=Math.abs(e.x-v.x)*.5*r.width;el.style.left=x+'px';el.style.top=y+'px';el.style.setProperty('--hd',(pr*2.3)+'px')}}

function resize(){if(!ok)return;const r=host.getBoundingClientRect(),w=Math.max(1,r.width),h=Math.max(1,r.height);R.setSize(w,h,false);cam.aspect=w/h;
  const vf=cam.fov*Math.PI/180,hf=2*Math.atan(Math.tan(vf/2)*cam.aspect);const dist=Math.max((W*1.01/2)/Math.tan(hf/2),2.9/2/Math.tan(vf/2));
  cam.position.set(0,.14,dist);cam.lookAt(0,-.11,0);cam.updateProjectionMatrix();placeHits();dirty=true}

function loop(){raf=requestAnimationFrame(loop);if(!ok||!dirty||!host.offsetParent)return;dirty=false;
  const th=THEMES[theme],p=built.parts,on=state.power?1:.06;
  p.relief.emissiveIntensity=(th.glowBase+th.glowGain*glow)*on;p.emblemLight.intensity=th.emblem?(1.2+3*glow)*on:0;p.lensMat.emissiveIntensity=3*on;p.spots.children.forEach(c=>{if(c.isSpotLight)c.intensity=2.2*on});p.spill.intensity=(.4+2.2*glow)*on*th.glowGain;
  p.jewel.emissiveIntensity=state.power?2.4:.15;p.jewelLight.intensity=state.power?.6:0;
  R.render(scene,cam)}

function init(container){
  if(!T)return false;
  try{host=container;const c=document.createElement('canvas');c.className='amp3d-canvas';host.appendChild(c);
    R=new T.WebGLRenderer({canvas:c,antialias:true,alpha:true,powerPreference:'high-performance'});
    R.setPixelRatio(Math.min(2,devicePixelRatio||1));R.outputColorSpace=T.SRGBColorSpace;R.toneMapping=T.ACESFilmicToneMapping;R.toneMappingExposure=1.12;
  }catch(e){return false}
  scene=new T.Scene();scene.environment=envMap();
  const key=new T.DirectionalLight(0xffffff,1.4);key.position.set(-2,4,6);scene.add(key);
  const rim=new T.DirectionalLight(0xbfd0ff,.6);rim.position.set(4,2,-3);scene.add(rim);
  cam=new T.PerspectiveCamera(22,2,.1,100);
  built=build();scene.add(built.group);
  overlay=document.createElement('div');overlay.className='amp3d-hits';host.appendChild(overlay);makeOverlay();
  ok=true;applyTheme(state.amp);
  KEYS.forEach(id=>bind(id,v=>{built.parts.knobs[id].rotation.z=-(-150+300*v/11)*Math.PI/180;dirty=true}));
  KEYS.forEach(id=>{built.parts.knobs[id].rotation.z=-(-150+300*state[id]/11)*Math.PI/180});
  const tg=id=>v=>{built.parts.toggles[id].userData.bat.rotation.x=v?-.45:.45;dirty=true};
  ['ampTight','ampBright'].forEach(id=>{bind(id,tg(id));tg(id)(state[id])});
  bind('amp',v=>applyTheme(v));bind('power',()=>dirty=true);
  new ResizeObserver(resize).observe(host);resize();
  document.fonts&&document.fonts.ready.then(()=>{for(const k in THEMES)delete cache['plate'+k];applyTheme(theme)});
  loop();return true}
return {init,setGlow(v){if(Math.abs(v-glow)>.004){glow=v;dirty=true}},get ok(){return ok},
  helpers:{cnv,tex,vnoise,fbm,sm,normalFromHeight,grayCanvas,G_PATH,envMap}};  // shared with Pedal3D
})();
