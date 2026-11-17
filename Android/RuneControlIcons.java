package org.runeclassic.port;
import android.graphics.*;
/** Existing touch-control artwork shared by controls and Laws of Battle. Coordinates use a 64-unit square. */
final class RuneControlIcons {
    static final int HAMMER=100, CORPSE_LIFT=101;
    static void line(Canvas c,Paint p,Path path,float... points){path.reset();path.moveTo(points[0],points[1]);for(int i=2;i<points.length;i+=2)path.lineTo(points[i],points[i+1]);c.drawPath(path,p);}
    static void draw(Canvas c,int id,Paint p,Path path){p.setColor(RuneMenuView.IVORY);p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(4);p.setStrokeCap(Paint.Cap.ROUND);p.setStrokeJoin(Paint.Join.ROUND);
            if(id==CORPSE_LIFT){ // Standing carrier holding a limp, horizontal body overhead.
                p.setStyle(Paint.Style.FILL);c.drawCircle(0,3,5,p);c.drawCircle(-24,-20,5,p);
                p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(5);
                line(c,p,path,0,13,0,23,-11,32);line(c,p,path,0,23,11,32);
                line(c,p,path,-3,14,-14,3,-12,-12);line(c,p,path,3,14,14,3,12,-13);
                p.setStrokeWidth(6);line(c,p,path,-15,-18,7,-17);
                p.setStrokeWidth(4.5f);line(c,p,path,7,-17,19,-24,29,-20);line(c,p,path,7,-17,19,-7,30,-9);
                line(c,p,path,-11,-18,-16,-5,-8,-2);line(c,p,path,-9,-18,-5,-28,3,-29);
            }
            else if(id==HAMMER){line(c,p,path,-4,26,-4,-5,4,-5,4,26,-4,26);line(c,p,path,-25,-25,25,-25,25,-5,-25,-5,-25,-25);line(c,p,path,-16,-25,-16,-5);line(c,p,path,16,-25,16,-5);}
            else if(id==4){line(c,p,path,-17,20,19,-22,24,-25,23,-15,-13,23);line(c,p,path,-19,10,-4,24);line(c,p,path,-20,25,-12,16);}
            else if(id==3){line(c,p,path,0,-26,23,-17,20,6,12,20,0,28,-12,20,-20,6,-23,-17,0,-26);line(c,p,path,0,-20,0,22);}
            else if(id==5){ // Side-on kicking figure, extended boot and planted rear foot.
                p.setStyle(Paint.Style.FILL);c.drawCircle(-14,-23,5.5f,p);
                p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(6);
                line(c,p,path,-14,-12,-8,2,-20,24);line(c,p,path,-24,26,-12,26);
                line(c,p,path,-8,2,7,-6,23,0);line(c,p,path,-12,-9,-27,-2);line(c,p,path,-11,-9,2,-15);
                path.reset();path.moveTo(19,-4);path.lineTo(28,-3);path.lineTo(32,5);path.lineTo(20,5);path.close();
                p.setStyle(Paint.Style.FILL);c.drawPath(path,p);
            }
            else if(id==6){ // Three ascending tiers; longest at the bottom.
                p.setStrokeWidth(5);line(c,p,path,-26,18,26,18);line(c,p,path,-17,0,17,0);line(c,p,path,-8,-18,8,-18);
            }
            else if(id==7){ // Raised axe gripped in a solid fist; arc shows the throwing motion.
                p.setStrokeWidth(5);line(c,p,path,-4,26,8,-23);
                path.reset();path.moveTo(7,-25);path.lineTo(28,-27);path.quadTo(21,-16,29,-5);path.lineTo(5,-12);path.close();
                p.setStyle(Paint.Style.FILL);c.drawPath(path,p);
                path.reset();path.moveTo(-25,26);path.lineTo(-15,12);path.lineTo(-15,6);path.lineTo(-10,1);
                path.lineTo(1,3);path.lineTo(6,9);path.lineTo(3,17);path.lineTo(-6,20);path.lineTo(-13,29);path.close();c.drawPath(path,p);
                p.setStyle(Paint.Style.STROKE);p.setColor(0xff181b1c);p.setStrokeWidth(2.5f);
                line(c,p,path,-10,7,2,9);line(c,p,path,-11,12,0,14);line(c,p,path,-6,3,4,8);
                p.setColor(RuneMenuView.IVORY);p.setStrokeWidth(3);
                path.reset();path.moveTo(-29,-5);path.quadTo(-25,-24,-10,-29);c.drawPath(path,p);
                line(c,p,path,-17,-29,-10,-29,-11,-22);
            }
            else if(id==8){ // Head-first dive: straight trailing legs, torso, arms reaching ahead.
                p.setStyle(Paint.Style.FILL);c.drawCircle(14,0,6,p);
                p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(9);
                line(c,p,path,-9,-7,4,6); // torso
                p.setStrokeWidth(5.5f);
                line(c,p,path,-12,-8,-30,-29);line(c,p,path,-6,-11,-22,-32); // two straight legs, no reversed knees
                line(c,p,path,0,4,15,16,29,28);line(c,p,path,5,7,20,15,32,24); // arms extended head-first
            }
            else if(id==2){line(c,p,path,-17,8,-22,-4,-18,-9,-10,0,-10,-23,-5,-26,-3,-24,-3,-5,-3,-30,3,-31,5,-28,5,-5,5,-25,11,-25,13,-21,13,-4,13,-17,19,-16,20,-10,20,13,13,25,-3,25,-17,8);}
            else if(id==RuneTouchLayout.POWER){ // Rune stone with a bright central sigil.
                line(c,p,path,0,-30,23,-9,16,19,0,29,-16,19,-23,-9,0,-30);
                line(c,p,path,-10,-8,0,-18,10,-8,0,11,-10,-8);
                line(c,p,path,0,11,0,22);
            }
        
    }
}
