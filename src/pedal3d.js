/* ================= Pedal3D: real-time WebGL pedalboard =================
   Die-cast enclosures with powder coat or metal flake, silkscreen decal per pedal, knurled knobs, chrome 3PDT
   footswitch, LED with real light spill, jacks and patch cables, soft contact shadows on a carpet board.
   Same interaction model as the amp: transparent HTML hit areas over the 3D knobs and footswitches. */
const Pedal3D=(()=>{
const T=window.THREE;
if(!T||typeof Amp3D==='undefined'||!Amp3D.helpers) return {create:()=>null};
const {cnv,tex,vnoise,fbm,normalFromHeight,grayCanvas,G_PATH,envMap}=Amp3D.helpers;
const PW=.72, PD=1.18, PH=.27, BT=.022, GAP=.5, TOP=PH+BT;   // enclosure width/depth/height, bevel, spacing, top surface
const KZ=-.36, LEDZ=.17, FSZ=.37;                            // knob row, LED, footswitch (pedal-local z)
const LF="'Archivo',sans-serif";
let S=null;                                                  // geometry/textures shared by every board

function shared(){ if(S) return S;
  const peel=(()=>{const N=256,n=vnoise(31,64),h=new Float32Array(N*N);for(let y=0;y<N;y++)for(let x=0;x<N;x++)h[y*N+x]=fbm(n,x/N*64,y/N*64,3);return tex(normalFromHeight(h,N,N,3,true),{rep:[3,5]})})();
  const flake=(()=>{const N=256,r=mulberry(9),h=new Float32Array(N*N);for(let i=0;i<h.length;i++)h[i]=r();return tex(normalFromHeight(h,N,N,1.6,true),{rep:[6,10]})})();
  const knurl=(()=>{const c=cnv(512,16),g=c.getContext('2d');for(let i=0;i<512;i+=8){g.fillStyle='#fff';g.fillRect(i,0,4,16);g.fillStyle='#000';g.fillRect(i+4,0,4,16)}
    const d=g.getImageData(0,0,512,16).data,h=new Float32Array(512*16);for(let i=0;i<h.length;i++)h[i]=d[i*4]/255;const t=tex(normalFromHeight(h,512,16,1.4,true),{rep:[3,1]});return t})();
  const carpet=(()=>{const N=512,n=vnoise(12,128),n2=vnoise(13,256),h=new Float32Array(N*N);for(let y=0;y<N;y++)for(let x=0;x<N;x++)h[y*N+x]=fbm(n,x/N*128,y/N*128,3)*.6+n2(x/N*256,y/N*256)*.4;
    return {normal:tex(normalFromHeight(h,N,N,3,true),{rep:[10,5]}),rough:tex(grayCanvas(h,N,N,v=>.8+v*.2),{rep:[10,5]})}})();
  const blob=(()=>{const c=cnv(256,256),g=c.getContext('2d');g.filter='blur(22px)';g.fillStyle='rgba(0,0,0,.85)';g.beginPath();g.roundRect(54,40,148,176,24);g.fill();return tex(c)})();
  const rr=(w,h,r)=>{const s=new T.Shape(),x=-w/2,y=-h/2;s.moveTo(x+r,y);s.lineTo(x+w-r,y);s.quadraticCurveTo(x+w,y,x+w,y+r);s.lineTo(x+w,y+h-r);s.quadraticCurveTo(x+w,y+h,x+w-r,y+h);s.lineTo(x+r,y+h);s.quadraticCurveTo(x,y+h,x,y+h-r);s.lineTo(x,y+r);s.quadraticCurveTo(x,y,x+r,y);return s};
  const body=new T.ExtrudeGeometry(rr(PW-2*BT,PD-2*BT,.05),{depth:PH,bevelEnabled:true,bevelThickness:BT,bevelSize:BT,bevelSegments:6,curveSegments:16});body.rotateX(-Math.PI/2);
  const prof=(pts)=>pts.map(p=>new T.Vector2(p[0],p[1]));
  const knob=new T.LatheGeometry(prof([[0,0],[.064,0],[.066,.005],[.066,.05],[.062,.059],[.052,.066],[0,.067]]),64);
  const cap=new T.LatheGeometry(prof([[0,0],[.044,0],[.044,.012],[.039,.022],[.024,.029],[0,.031]]),48);
  S={peel,flake,knurl,carpet,blob,body,knob,cap,
    nut:new T.CylinderGeometry(.052,.052,.018,6),bush:new T.CylinderGeometry(.031,.031,.032,24),knobNut:new T.CylinderGeometry(.03,.03,.008,6),
    bezel:new T.TorusGeometry(.021,.006,12,32),lens:new T.SphereGeometry(.016,24,12,0,Math.PI*2,0,Math.PI/2),
    jackNut:new T.CylinderGeometry(.03,.03,.012,6),plug:new T.CylinderGeometry(.024,.022,.12,20),ptr:new T.BoxGeometry(.009,.003,.038),
    chrome:new T.MeshStandardMaterial({color:0xe8e8ea,metalness:1,roughness:.14}),
    rubber:new T.MeshStandardMaterial({color:0x0c0c0d,roughness:.62}),
    bottom:new T.MeshStandardMaterial({color:0x101010,roughness:.8})};
  return S}

/* ---------- silkscreen: name, labels, scales and per-pedal artwork ---------- */
function decal(d){
  const w=1024,h=Math.round(1024*PD/PW),c=cnv(w,h),g=c.getContext('2d'),px=w/PW,X=x=>(x+PW/2)*px,Z=z=>(z+PD/2)*px,ink=d.ink;
  g.strokeStyle=ink;g.fillStyle=ink;g.globalAlpha=.55;g.lineWidth=3;g.beginPath();g.roundRect(30,30,w-60,h-60,28);g.stroke();g.globalAlpha=1;
  // artwork behind the name
  g.save();g.globalAlpha=.9;ART[d.art]&&ART[d.art](g,w,h,Z,d);g.restore();
  // knob labels + scales
  const kx=knobX(d.knobs.length),kr=d.knobs.length>3?.054:.066;
  g.textAlign='center';g.textBaseline='middle';if('fontStretch' in g)g.fontStretch='expanded';
  d.knobs.forEach((k,i)=>{const cx=X(kx[i]),cy=Z(KZ);
    for(let t=0;t<=10;t++){const a=(-150+300*t/10)*Math.PI/180,r=(kr+.03)*px;g.beginPath();g.arc(cx+Math.sin(a)*r,cy-Math.cos(a)*r,t%5===0?6:3.5,0,7);g.fill()}
    g.font=`800 ${Math.round(px*(d.knobs.length>3?.028:.032))}px ${LF}`;g.fillText(k.l,cx,cy+(kr+.075)*px)});
  // name
  if('fontStretch' in g)g.fontStretch=d.stretch||'normal';
  g.font=`${d.weight||400} ${Math.round(px*d.size)}px ${d.font}`;if('letterSpacing' in g)g.letterSpacing=(d.ls||0)+'px';
  if(d.glow){g.shadowColor=d.glow;g.shadowBlur=24}
  d.name.split('\n').forEach((ln,i,a)=>g.fillText(ln,w/2,Z(-.02)+(i-(a.length-1)/2)*px*d.size*1.05));g.shadowBlur=0;
  if('letterSpacing' in g)g.letterSpacing='6px';if('fontStretch' in g)g.fontStretch='expanded';
  g.font=`700 ${Math.round(px*.024)}px ${LF}`;g.globalAlpha=.85;g.fillText(d.sub.toUpperCase(),w/2,Z(.095));g.globalAlpha=1;
  g.font=`800 ${Math.round(px*.02)}px ${LF}`;g.fillText('ON',X(.075),Z(LEDZ));
  // brand line + g at the toe
  g.font=`800 ${Math.round(px*.022)}px ${LF}`;if('letterSpacing' in g)g.letterSpacing='10px';g.fillText('GHOSTLIKE',w/2,Z(.52));
  const s=.05*px/289*2.2;g.save();g.translate(X(-.27)-86*s,Z(.5)-144*s);g.scale(s,s);g.fill(new Path2D(G_PATH));g.restore();
  return tex(c,{srgb:true})}
const ART={
  gate(g,w,h,Z){const n=14,bw=w*.035;for(let i=0;i<n;i++){const x=w*.5+(i-(n-1)/2)*bw*1.5,hh=(.15+.85*Math.pow(i/(n-1),1.4))*h*.07;g.globalAlpha=i>9?.95:.35;g.fillRect(x-bw/2,Z(.03)+h*.045-hh,bw,hh)}},
  scream(g,w,h,Z,d){const r=mulberry(4);g.lineWidth=7;g.lineJoin='miter';g.strokeStyle=d.accent;g.globalAlpha=.85;g.beginPath();
    for(let i=0;i<=28;i++){const x=w*.08+i*w*.84/28,y=Z(-.02)+(i%2?-1:1)*h*(.02+.05*r()*Math.sin(i/28*Math.PI));i?g.lineTo(x,y):g.moveTo(x,y)}g.stroke()},
  phantom(g,w,h,Z,d){g.lineWidth=5;[[0,.9],[1.3,.55],[2.6,.3]].forEach(([ph,a])=>{g.globalAlpha=a;g.strokeStyle=d.accent;g.beginPath();
    for(let x=w*.08;x<=w*.92;x+=4){const y=Z(-.02)+Math.sin(x/w*Math.PI*5+ph)*h*.035*(1-Math.abs(x/w-.5)*1.2);x>w*.08?g.lineTo(x,y):g.moveTo(x,y)}g.stroke()})},
  echo(g,w,h,Z,d){const p=new Path2D(G_PATH),s=h*.15/289;[.05,.08,.12,.18].forEach((a,i)=>{g.save();g.globalAlpha=a;g.fillStyle=d.accent;g.translate(w*(.3+i*.13)-86*s,Z(-.03)-144*s);g.scale(s,s);g.fill(p);g.restore()})},
  ether(g,w,h,Z,d){g.strokeStyle=d.accent;g.lineWidth=4;for(let i=0;i<6;i++){g.globalAlpha=.65-i*.09;const rw=w*(.12+i*.065),y0=Z(.06);g.beginPath();g.moveTo(w/2-rw,y0);g.lineTo(w/2-rw,y0-h*.03);
    g.quadraticCurveTo(w/2-rw,y0-h*(.08+i*.018),w/2,y0-h*(.1+i*.022));g.quadraticCurveTo(w/2+rw,y0-h*(.08+i*.018),w/2+rw,y0-h*.03);g.lineTo(w/2+rw,y0);g.stroke()}}
};
const knobX=n=>n>3?[-.255,-.085,.085,.255]:n===2?[-.15,.15]:[-.21,0,.21];

/* ---------- one board (= one canvas) ---------- */
function create(host,defs){
  if(!T) return null;
  let R;try{const c=document.createElement('canvas');c.className='amp3d-canvas';host.appendChild(c);
    R=new T.WebGLRenderer({canvas:c,antialias:true,alpha:true,powerPreference:'high-performance'});}catch(e){return null}
  R.setPixelRatio(Math.min(2,devicePixelRatio||1));R.outputColorSpace=T.SRGBColorSpace;R.toneMapping=T.ACESFilmicToneMapping;R.toneMappingExposure=1.05;
  const P=shared(),scene=new T.Scene();scene.environment=envMap(R);
  const key=new T.DirectionalLight(0xffffff,1.6);key.position.set(-1.5,5,3);scene.add(key);
  const rim=new T.DirectionalLight(0xc8d6ff,.5);rim.position.set(3,2,-4);scene.add(rim);
  const cam=new T.PerspectiveCamera(24,2,.1,60);
  const floor=new T.Mesh(new T.PlaneGeometry(14,7),new T.MeshStandardMaterial({color:0x17181a,roughness:1,normalMap:P.carpet.normal,roughnessMap:P.carpet.rough,normalScale:new T.Vector2(.7,.7)}));
  floor.rotation.x=-Math.PI/2;scene.add(floor);
  const n=defs.length,span=n*PW+(n-1)*GAP,parts=[],hits=[];let dirty=true;
  const overlay=document.createElement('div');overlay.className='amp3d-hits';host.appendChild(overlay);
  const ang=(id,v)=>-(-150+300*norm(id,v))*Math.PI/180;
  defs.forEach((d,i)=>{
    const g=new T.Group();g.position.x=-span/2+PW/2+i*(PW+GAP);scene.add(g);
    const sh=new T.Mesh(new T.PlaneGeometry(PW+.55,PD+.5),new T.MeshBasicMaterial({map:P.blob,transparent:true,depthWrite:false}));sh.rotation.x=-Math.PI/2;sh.position.y=.002;g.add(sh);
    const coat=new T.MeshPhysicalMaterial({color:new T.Color(d.color),roughness:d.flake?.3:.42,metalness:d.flake?.55:.22,clearcoat:.7,clearcoatRoughness:d.flake?.12:.3,
      normalMap:d.flake?P.flake:P.peel,normalScale:new T.Vector2(d.flake?.35:.22,d.flake?.35:.22)});
    const body=new T.Mesh(P.body,[coat,coat]);g.add(body);   // extrusion spans y -BT..PH+BT, top face at TOP
    const top=new T.Mesh(new T.PlaneGeometry(PW-2*BT-.004,PD-2*BT-.004),new T.MeshStandardMaterial({map:decal(d),transparent:true,roughness:.35,polygonOffset:true,polygonOffsetFactor:-2}));
    top.rotation.x=-Math.PI/2;top.position.y=TOP+.0006;g.add(top);
    const part={g,top,d,knobs:{},led:null,ledLight:null,cap:null};
    // knobs
    const kx=knobX(d.knobs.length),ks=d.knobs.length>3?.82:1;
    const kMat=new T.MeshPhysicalMaterial({color:new T.Color(d.knob||'#111214'),roughness:.36,clearcoat:.45,clearcoatRoughness:.3,normalMap:P.knurl,normalScale:new T.Vector2(.7,.7)});
    const pMat=new T.MeshStandardMaterial({color:new T.Color(d.knobPtr||'#f2efe6'),roughness:.4,emissive:0x111111});
    d.knobs.forEach((k,j)=>{const kg=new T.Group();kg.position.set(kx[j],TOP,KZ);kg.scale.setScalar(ks);
      const nut=new T.Mesh(P.knobNut,P.chrome);nut.position.y=.004;kg.add(nut);
      const rot=new T.Group();rot.position.y=.008;const kn=new T.Mesh(P.knob,kMat);rot.add(kn);const pt=new T.Mesh(P.ptr,pMat);pt.position.set(0,.0675,-.03);rot.add(pt);kg.add(rot);g.add(kg);
      part.knobs[k.p]=rot;rot.rotation.y=ang(k.p,state[k.p]);
      bind(k.p,v=>{rot.rotation.y=ang(k.p,v);dirty=true});
      const el=document.createElement('div');el.className='kn hit';el.dataset.p=k.p;el.dataset.l=k.l;overlay.appendChild(el);mountKnob(el);
      hits.push([el,new T.Vector3(g.position.x+kx[j],TOP+.06,KZ),.075*ks])});
    // LED with bezel and light spill
    const bz=new T.Mesh(P.bezel,P.chrome);bz.rotation.x=Math.PI/2;bz.position.set(0,TOP+.004,LEDZ);g.add(bz);
    const lm=new T.MeshStandardMaterial({color:new T.Color(d.led).multiplyScalar(.35),emissive:new T.Color(d.led),emissiveIntensity:0,roughness:.2});
    const lens=new T.Mesh(P.lens,lm);lens.position.set(0,TOP+.002,LEDZ);g.add(lens);
    const ll=new T.PointLight(new T.Color(d.led),0,.22,2);ll.position.set(0,TOP+.03,LEDZ);g.add(ll);
    // footswitch
    const fs=new T.Group();fs.position.set(0,TOP,FSZ);const hn=new T.Mesh(P.nut,P.chrome);hn.position.y=.009;fs.add(hn);
    const bu=new T.Mesh(P.bush,P.chrome);bu.position.y=.034;fs.add(bu);const cp=new T.Mesh(P.cap,P.chrome);cp.position.y=.05;fs.add(cp);g.add(fs);
    const setOn=v=>{lm.emissiveIntensity=v?3.2:0;ll.intensity=v?.12:0;dirty=true};bind(d.sw,setOn);setOn(state[d.sw]);
    const b=document.createElement('button');b.className='hitbtn';b.setAttribute('aria-label',d.name.replace('\n',' ')+' an/aus');
    b.addEventListener('click',()=>{set(d.sw,!state[d.sw]);cp.position.y=.043;dirty=true;setTimeout(()=>{cp.position.y=.05;dirty=true},130)});
    overlay.appendChild(b);hits.push([b,new T.Vector3(g.position.x,TOP+.07,FSZ),.075]);
    // jacks + plugs on both sides, DC jack at the back
    for(const sx of [-1,1]){const jx=sx*(PW/2+.004),jy=BT+PH*.55,jz=-.2;
      const jn=new T.Mesh(P.jackNut,P.chrome);jn.rotation.z=Math.PI/2;jn.position.set(jx+sx*.006,jy,jz);g.add(jn);
      const pl=new T.Mesh(P.plug,P.rubber);pl.rotation.z=Math.PI/2;pl.position.set(jx+sx*.07,jy,jz);g.add(pl);
      const ring=new T.Mesh(new T.CylinderGeometry(.026,.026,.018,20),P.chrome);ring.rotation.z=Math.PI/2;ring.position.set(jx+sx*.019,jy,jz);g.add(ring)}
    const dc=new T.Mesh(new T.CylinderGeometry(.022,.022,.03,20),P.rubber);dc.rotation.x=Math.PI/2;dc.position.set(.18,BT+PH*.62,-PD/2-.01);g.add(dc);
    parts.push(part)});
  // patch cables: plug end → down onto the carpet → up into the next pedal (outer ones leave the board)
  const cable=(a,b,lift=.0)=>{const m=a.clone().lerp(b,.5);m.y=.03+lift;const c=new T.CatmullRomCurve3([a,a.clone().add(new T.Vector3(Math.sign(b.x-a.x)*.07,-.04,0)),m,b.clone().add(new T.Vector3(Math.sign(a.x-b.x)*.07,-.04,0)),b]);
    scene.add(new T.Mesh(new T.TubeGeometry(c,64,.017,10),P.rubber))};
  const jy=BT+PH*.55,jz=-.2;
  for(let i=0;i<n;i++){const x=-span/2+PW/2+i*(PW+GAP);
    if(i<n-1) cable(new T.Vector3(x+PW/2+.13,jy,jz),new T.Vector3(x+PW+GAP-PW/2-.13,jy,jz));
    if(i===0) cable(new T.Vector3(x-PW/2-.13,jy,jz),new T.Vector3(x-PW/2-1.6,.02,jz+.35));
    if(i===n-1) cable(new T.Vector3(x+PW/2+.13,jy,jz),new T.Vector3(x+PW/2+1.6,.02,jz+.35))}
  const place=()=>{const r=host.getBoundingClientRect();for(const [el,pos,rad] of hits){const v=pos.clone().project(cam),e=pos.clone().add(new T.Vector3(rad,0,0)).project(cam);
    el.style.left=(v.x*.5+.5)*r.width+'px';el.style.top=(-v.y*.5+.5)*r.height+'px';el.style.setProperty('--hd',(Math.abs(e.x-v.x)*.5*r.width*2.3)+'px')}};
  const resize=()=>{const r=host.getBoundingClientRect(),w=Math.max(1,r.width),h=Math.max(1,r.height);R.setSize(w,h,false);cam.aspect=w/h;
    // fit the row of pedals: width plus a strip of cable, and the pedal depth as seen from the camera's elevation
    const el=54*Math.PI/180,vf=cam.fov*Math.PI/180,hf=2*Math.atan(Math.tan(vf/2)*cam.aspect),ext=(PD+.22)*Math.sin(el)+(PH+.12)*Math.cos(el),dist=Math.max((span+.45)/2/Math.tan(hf/2),ext/2/Math.tan(vf/2)*1.16);
    const tgt=new T.Vector3(0,.1,-.01);cam.position.set(0,tgt.y+dist*Math.sin(el),tgt.z+dist*Math.cos(el));cam.lookAt(tgt);cam.updateProjectionMatrix();place();dirty=true};
  new ResizeObserver(resize).observe(host);resize();
  document.fonts&&document.fonts.ready.then(()=>{parts.forEach(p=>{p.top.material.map=decal(p.d);p.top.material.needsUpdate=true});dirty=true});
  (function loop(){requestAnimationFrame(loop);if(!dirty||!host.offsetParent)return;dirty=false;R.render(scene,cam)})();
  return {ok:true,refresh(){resize()}}}

return {create};
})();
