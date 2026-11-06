// M202MD28AA desktop enclosure, mm. Local X=right,Y=up,Z=rear.
// Datasheet PDF p28: PCB215x50, holes207x42, active160.5x23.9.
part="assembly"; // body, lid, stand, assembly, exploded
$fn=48;
W=232; H=68; D=48; wall=2.4; tilt=15;
pcb_back=18.9; // front glass approx5.1; CHECK actual modified module
module box(w,h,d,x=0,y=0,z=0){translate([x-w/2,y-h/2,z])cube([w,h,d]);}
module hole(x,y,z,d,h){translate([x,y,z])cylinder(d=d,h=h);}
module body(){difference(){union(){difference(){box(W,H,D-wall);box(W-2*wall,H-2*wall,D,0,0,wall);}
for(x=[-111,111],y=[-29,29])hole(x,y,wall,8,D-2*wall);}
// Oversized window for oblique viewing, active area center offset -4,-4.
box(168,32,5,-4,-4,-1);
for(x=[-111,111],y=[-29,29])hole(x,y,31,2.6,18);
// Rear-side ventilation; narrow slots keep fingers out.
for(x=[-78:12:78])box(7,6,2, x, H/2-1,35);
// Generic rearward cable notch; not a fitted USB socket.
box(12,8,9,0,-H/2+2,D-8);
}}
module lid(){difference(){union(){box(W,H,wall,0,0,D-wall);
for(x=[-103.5,103.5],y=[-21,21])hole(x,y,pcb_back,5.5,D-wall-pcb_back+0.1);}
for(x=[-103.5,103.5],y=[-21,21])hole(x,y,pcb_back-0.1,2.6,13);
for(x=[-111,111],y=[-29,29])hole(x,y,D-wall-1,3.4,wall+2);
for(x=[-75:10:75],y=[-22,22])box(5,2,5,x,y,D-wall-1);
}}
module pose(){multmatrix([[1,0,0,0],[0,sin(tilt),cos(tilt),0],[0,cos(tilt),-sin(tilt),49.3],[0,0,0,1]])children();}
module wedge(){rotate([90,0,90])linear_extrude(12,center=true)polygon([[-12,4],[-12,19],[58,4]]);}
module stand(){difference(){union(){box(232,74,4,0,23,0);for(x=[-82,82])translate([x,0,0])wedge();}
pose()box(W+1,H+100,D+0.6,0,50,-0.3);
// Four underside adhesive rubber-foot pockets.
for(x=[-101,101],y=[-4,50])hole(x,y,-0.1,10,0.9);
}}
module display(){color([0.12,0.35,0.24])box(215,50,1.6,0,0,17.3);color([0.04,0.09,0.08])box(195.2,40,12.2,-7.5,0,5.1);color([0.1,0.85,0.65])box(160.5,23.9,0.2,-4,-4,4.9);}
if(part=="body")body();
else if(part=="lid")translate([0,0,D])rotate([180,0,0])lid();
else if(part=="stand")stand();
else {color([0.22,0.24,0.26])pose()body();color([0.32,0.34,0.36])pose()translate([0,0,part=="exploded"?30:0])lid();pose()display();color([0.12,0.13,0.14])stand();}
