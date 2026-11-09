package org.runeclassic.port;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.graphics.*;
import android.os.SystemClock;
import android.view.*;
import android.widget.*;
import java.util.HashSet;

/** Independently editable controls; Android split events retain simultaneous stick/look/actions. */
final class RuneTouchView extends FrameLayout {
    interface Host {void touchKey(int code,boolean down);void touchMove(float x,float y);void touchLook(float x,float y);void touchCamera(float x,float y);void touchMenu();void closeTouchEditor();}
    final Host host;
    final SharedPreferences prefs;
    RuneTouchLayout saved,layout;
    final Control[] buttons=new Control[RuneTouchLayout.COUNT];
    final FrameLayout inspector;
    final Button panels;
    final TextView selectedLabel,sizeLabel,opacityLabel;
    final SeekBar sizeSlider,opacitySlider;
    boolean editing,shown=true,kick=true,updating,grabbing,resizing,controllerCursor;
    int selected=RuneTouchLayout.ATTACK,cornerX=1,cornerY=1;
    float startX,startY,startLeft,startTop,startW,startH,startSize,cursorX=800,cursorY=450,axisX,axisY;
    long lastTime;
    boolean cameraJumpHeld;
    final HashSet<Integer> publishedKeys=new HashSet<>();
    final Runnable finishCameraJump=()->{cameraJumpHeld=false;publishKey(KeyEvent.KEYCODE_SPACE);};
    void publishKey(int code){
        if(code==0)return;boolean down=code==KeyEvent.KEYCODE_SPACE&&cameraJumpHeld;
        for(Control b:buttons)if(b!=null&&b.keyHeld&&CODES[b.id]==code)down=true;
        if(down?publishedKeys.add(code):publishedKeys.remove(code))host.touchKey(code,down);
    }
    void cameraJump(){if(editing||!shown)return;cameraJumpHeld=true;publishKey(KeyEvent.KEYCODE_SPACE);removeCallbacks(finishCameraJump);postDelayed(finishCameraJump,80);}

    int bloodlustMoves;
    void setBloodlustMoves(int mask){
        int repaint=bloodlustMoves|mask;bloodlustMoves=mask;
        if((repaint&1)!=0)buttons[RuneTouchLayout.JUMP].invalidate();
        if((repaint&2)!=0){buttons[RuneTouchLayout.BLOCK].invalidate();buttons[RuneTouchLayout.ATTACK].invalidate();}
    }
    boolean bloodlustCue(int id){return !editing && shown && ((id==RuneTouchLayout.JUMP&&(bloodlustMoves&1)!=0)||((id==RuneTouchLayout.BLOCK||id==RuneTouchLayout.ATTACK)&&(bloodlustMoves&2)!=0));}

    static final int GOLD=0xffd3af70,IVORY=0xffeee8db;
    static final int[] CODES={0,0,KeyEvent.KEYCODE_CTRL_LEFT,KeyEvent.KEYCODE_CTRL_RIGHT,KeyEvent.KEYCODE_ENTER,KeyEvent.KEYCODE_F,KeyEvent.KEYCODE_SPACE,KeyEvent.KEYCODE_Q,KeyEvent.KEYCODE_C,0,0,KeyEvent.KEYCODE_E};
    RuneTouchView(Context c,SharedPreferences p,Host h){
        super(c);host=h;prefs=p;layout=saved=RuneTouchLayout.load(p);setMotionEventSplittingEnabled(true);setClipChildren(false);setFocusableInTouchMode(true);
        // Look goes underneath buttons, so overlaps never steal action touches.
        buttons[9]=new Control(c,9);addView(buttons[9]);
        for(int i=0;i<RuneTouchLayout.COUNT;i++){if(i==9)continue;buttons[i]=new Control(c,i);addView(buttons[i]);}
        inspector=new FrameLayout(c);inspector.setBackground(new RuneMenuView.Iron(false,true));addView(inspector,new FrameLayout.LayoutParams(540,354));
        text("Edit touch controls",18,8,504,40,27);
        text("Drag to move · pull a corner to resize",18,47,504,30,18);
        selectedLabel=text("",18,83,504,32,22);
        sizeLabel=text("",22,122,150,34,20);sizeSlider=slider("Selected control size",168,120,346,50,150);
        opacityLabel=text("",22,172,150,34,20);opacitySlider=slider("Selected control opacity",168,170,346,50,80);
        sizeSlider.setOnSeekBarChangeListener(listener(true));opacitySlider.setOnSeekBarChangeListener(listener(false));
        button("Reset layout",18,229,170,48,()->{layout=new RuneTouchLayout();refresh();});
        button("Cancel",196,229,150,48,()->finish(false));button("Done",354,229,168,48,()->finish(true));
        button("‹ Control",18,291,170,44,()->select((selected+RuneTouchLayout.COUNT-1)%RuneTouchLayout.COUNT));button("Control ›",196,291,170,44,()->select((selected+1)%RuneTouchLayout.COUNT));
        panels=new Button(c);panels.setText("Show editor");panels.setAllCaps(false);panels.setPadding(0,0,0,0);panels.setMinHeight(0);panels.setMinimumHeight(0);panels.setIncludeFontPadding(false);panels.setTextColor(IVORY);panels.setBackground(new RuneMenuView.Iron(false,true));panels.setOnClickListener(v->{inspector.setVisibility(VISIBLE);panels.setVisibility(GONE);});addView(panels);panels.setVisibility(GONE);inspector.setVisibility(GONE);
        button("Hide",374,291,148,44,()->{inspector.setVisibility(GONE);panels.setVisibility(VISIBLE);});
        addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or,ob)->arrange());refresh();
    }
    TextView text(String s,int x,int y,int w,int h,int size){TextView t=new TextView(getContext());t.setText(s);t.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX,size);t.setTextColor(IVORY);t.setTypeface(RuneMenuView.BODY_FONT);t.setGravity(Gravity.CENTER);place(inspector,t,x,y,w,h);return t;}
    void place(FrameLayout parent,View v,int x,int y,int w,int h){FrameLayout.LayoutParams p=new FrameLayout.LayoutParams(w,h);p.leftMargin=x;p.topMargin=y;parent.addView(v,p);}
    Button button(String s,int x,int y,int w,int h,Runnable r){Button b=new Button(getContext());b.setText(s);b.setAllCaps(false);b.setTextColor(IVORY);b.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX,20);b.setPadding(2,0,2,0);b.setBackground(new RuneMenuView.Iron(false,false));b.setOnClickListener(v->r.run());place(inspector,b,x,y,w,h);return b;}
    SeekBar slider(String name,int x,int y,int w,int h,int max){SeekBar b=new SeekBar(getContext());b.setMax(max);b.setContentDescription(name);b.setProgressTintList(ColorStateList.valueOf(GOLD));b.setThumbTintList(ColorStateList.valueOf(GOLD));place(inspector,b,x,y,w,h);return b;}
    SeekBar.OnSeekBarChangeListener listener(boolean size){return new SeekBar.OnSeekBarChangeListener(){public void onProgressChanged(SeekBar b,int p,boolean user){if(user&&!updating){if(size)layout.items[selected].size=(p+50)/100f;else layout.items[selected].opacity=(p+20)/100f;refresh();}}public void onStartTrackingTouch(SeekBar b){}public void onStopTrackingTouch(SeekBar b){}};}
    float unit(){return Math.max(.1f,Math.min(getWidth()/1600f,getHeight()/1080f));}
    float vw(){return getWidth()/unit();}float vh(){return getHeight()/unit();}
    void arrange(){if(getWidth()==0||getHeight()==0)return;float u=unit();for(int i=0;i<RuneTouchLayout.COUNT;i++){float s=layout.scale(i,vw(),vh());FrameLayout.LayoutParams p=new FrameLayout.LayoutParams(Math.round(RuneTouchLayout.WIDTH[i]*s*u),Math.round(RuneTouchLayout.HEIGHT[i]*s*u));p.leftMargin=Math.round(layout.left(i,vw(),vh())*u);p.topMargin=Math.round(layout.top(i,vw(),vh())*u);buttons[i].setLayoutParams(p);buttons[i].invalidate();}
        inspector.setPivotX(0);inspector.setPivotY(0);inspector.setScaleX(u);inspector.setScaleY(u);inspector.setTranslationX((getWidth()-540*u)/2);inspector.setTranslationY(100*u);
        FrameLayout.LayoutParams p=new FrameLayout.LayoutParams(Math.round(190*u),Math.round(52*u));p.gravity=Gravity.TOP|Gravity.CENTER_HORIZONTAL;p.topMargin=Math.round(140*u);panels.setLayoutParams(p);panels.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX,22*u);
    }
    void configure(boolean show,boolean allowKick){if(shown&&!show)release();shown=show;kick=allowKick;refresh();}
    void globalOpacity(float alpha){for(int i=0;i<RuneTouchLayout.COUNT;i++)if(i!=1)saved.items[i].opacity=alpha;saved.save(prefs);if(!editing)layout=saved;refresh();}
    void refresh(){updating=true;selectedLabel.setText("Selected: "+RuneTouchLayout.NAMES[selected]);sizeLabel.setText("Size  "+Math.round(layout.items[selected].size*100)+"%");opacityLabel.setText("Opacity  "+Math.round(layout.items[selected].opacity*100)+"%");sizeSlider.setProgress(Math.round(layout.items[selected].size*100)-50);opacitySlider.setProgress(Math.round(layout.items[selected].opacity*100)-20);updating=false;
        for(int i=0;i<RuneTouchLayout.COUNT;i++){buttons[i].setVisibility(editing||shown||i==1?VISIBLE:GONE);buttons[i].setEnabled(editing||i!=5||kick);buttons[i].setAlpha(editing?1:layout.items[i].opacity*(i==5&&!kick?.4f:1));}arrange();invalidate();
    }
    void begin(){release();layout=saved.copy();editing=true;grabbing=false;controllerCursor=false;inspector.setVisibility(VISIBLE);panels.setVisibility(GONE);setVisibility(VISIBLE);select(4);requestFocus();}
    void finish(boolean save){release();if(save){saved=layout.copy();saved.save(prefs);}layout=saved;editing=false;inspector.setVisibility(GONE);panels.setVisibility(GONE);refresh();host.closeTouchEditor();}
    void select(int i){selected=i;refresh();}
    void release(){removeCallbacks(finishCameraJump);cameraJumpHeld=false;for(Control b:buttons)if(b!=null)b.release();publishKey(KeyEvent.KEYCODE_SPACE);axisX=axisY=0;grabbing=false;removeCallbacks(cursorTick);}
    boolean pick(float x,float y){
        Control b=buttons[selected];float u=unit();float l=b.getLeft()/u,t=b.getTop()/u,r=b.getRight()/u,bot=b.getBottom()/u;
        resizing=(Math.abs(x-l)<22||Math.abs(x-r)<22)&&(Math.abs(y-t)<22||Math.abs(y-bot)<22);
        if(resizing){cornerX=Math.abs(x-l)<Math.abs(x-r)?-1:1;cornerY=Math.abs(y-t)<Math.abs(y-bot)?-1:1;}
        else{int hit=-1;for(int i=RuneTouchLayout.COUNT-1;i>=0;i--)if(i!=9&&buttons[i].getLeft()/u<=x&&buttons[i].getRight()/u>=x&&buttons[i].getTop()/u<=y&&buttons[i].getBottom()/u>=y){hit=i;break;}if(hit<0&&buttons[9].getLeft()/u<=x&&buttons[9].getRight()/u>=x&&buttons[9].getTop()/u<=y&&buttons[9].getBottom()/u>=y)hit=9;if(hit<0)return false;select(hit);}
        b=buttons[selected];startX=x;startY=y;startLeft=b.getLeft()/u;startTop=b.getTop()/u;startW=b.getWidth()/u;startH=b.getHeight()/u;startSize=layout.items[selected].size;grabbing=true;return true;
    }
    void drag(float x,float y){if(!grabbing)return;if(resizing){float bw=RuneTouchLayout.WIDTH[selected],bh=RuneTouchLayout.HEIGHT[selected];layout.items[selected].size=RuneTouchLayout.clamp(startSize+((x-startX)*cornerX*bw+(y-startY)*cornerY*bh)/(bw*bw+bh*bh),.5f,2);float s=layout.scale(selected,vw(),vh());layout.move(selected,startLeft+(cornerX<0?startW-bw*s:0),startTop+(cornerY<0?startH-bh*s:0),vw(),vh());}else layout.move(selected,startLeft+x-startX,startTop+y-startY,vw(),vh());refresh();}
    int editPointer=-1;
    @Override public boolean onInterceptTouchEvent(MotionEvent e){if(!editing)return false;if(e.getActionMasked()==MotionEvent.ACTION_DOWN){float x=e.getX(),y=e.getY();boolean panel=inspector.getVisibility()==VISIBLE&&x>=inspector.getTranslationX()&&x<=inspector.getTranslationX()+540*unit()&&y>=inspector.getTranslationY()&&y<=inspector.getTranslationY()+354*unit();boolean toggle=panels.getVisibility()==VISIBLE&&x>=panels.getLeft()&&x<=panels.getRight()&&y>=panels.getTop()&&y<=panels.getBottom();return !panel&&!toggle;}return false;}
    @Override public boolean onTouchEvent(MotionEvent e){if(!editing)return false;controllerCursor=false;int a=e.getActionMasked();if(a==MotionEvent.ACTION_DOWN){editPointer=e.getPointerId(0);pick(e.getX()/unit(),e.getY()/unit());}else if(a==MotionEvent.ACTION_MOVE){int n=e.findPointerIndex(editPointer);if(n>=0)drag(e.getX(n)/unit(),e.getY(n)/unit());}else if(a==MotionEvent.ACTION_CANCEL||a==MotionEvent.ACTION_UP||(a==MotionEvent.ACTION_POINTER_UP&&e.getPointerId(e.getActionIndex())==editPointer)){grabbing=false;editPointer=-1;}return true;}
    @Override protected void dispatchDraw(Canvas c){if(editing){c.drawColor(0x44000000);Paint p=new Paint();p.setColor(0x12d3af70);for(float x=0;x<getWidth();x+=50*unit())c.drawLine(x,0,x,getHeight(),p);for(float y=0;y<getHeight();y+=50*unit())c.drawLine(0,y,getWidth(),y,p);}super.dispatchDraw(c);if(editing&&controllerCursor){Paint p=new Paint(3);p.setColor(IVORY);p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(2*unit());c.drawCircle(cursorX*unit(),cursorY*unit(),10*unit(),p);}}
    boolean key(KeyEvent e){int k=e.getKeyCode();boolean down=e.getAction()==KeyEvent.ACTION_DOWN;if(k==KeyEvent.KEYCODE_BUTTON_B||k==KeyEvent.KEYCODE_BACK||k==KeyEvent.KEYCODE_ESCAPE){if(down&&e.getRepeatCount()==0)finish(false);return true;}if(k==KeyEvent.KEYCODE_BUTTON_START){if(down&&e.getRepeatCount()==0)finish(true);return true;}
        if(k==KeyEvent.KEYCODE_BUTTON_L1||k==KeyEvent.KEYCODE_BUTTON_R1){if(down)select((selected+(k==KeyEvent.KEYCODE_BUTTON_L1?RuneTouchLayout.COUNT-1:1))%RuneTouchLayout.COUNT);return true;}
        if(k==KeyEvent.KEYCODE_BUTTON_A||k==KeyEvent.KEYCODE_BUTTON_THUMBL){controllerCursor=true;if(down&&e.getRepeatCount()==0){if(!clickPanel(cursorX*unit(),cursorY*unit()))pick(cursorX,cursorY);}else if(!down)grabbing=false;return true;}
        if(k>=KeyEvent.KEYCODE_DPAD_UP&&k<=KeyEvent.KEYCODE_DPAD_RIGHT){controllerCursor=true;if(down){cursorX=RuneTouchLayout.clamp(cursorX+(k==21?-12:k==22?12:0),0,vw());cursorY=RuneTouchLayout.clamp(cursorY+(k==19?-12:k==20?12:0),0,vh());drag(cursorX,cursorY);invalidate();}return true;}return true;
    }
    boolean clickPanel(float x,float y){if(panels.getVisibility()==VISIBLE&&x>=panels.getLeft()&&x<=panels.getRight()&&y>=panels.getTop()&&y<=panels.getBottom()){panels.performClick();return true;}if(inspector.getVisibility()!=VISIBLE)return false;x=(x-inspector.getTranslationX())/unit();y=(y-inspector.getTranslationY())/unit();if(x<0||x>540||y<0||y>354)return false;for(int i=0;i<inspector.getChildCount();i++){View v=inspector.getChildAt(i);if(x>=v.getLeft()&&x<=v.getRight()&&y>=v.getTop()&&y<=v.getBottom()){if(v instanceof Button)v.performClick();else if(v instanceof SeekBar){float ratio=RuneTouchLayout.clamp((x-v.getLeft())/v.getWidth(),0,1);if(v==sizeSlider)layout.items[selected].size=.5f+1.5f*ratio;else layout.items[selected].opacity=.2f+.8f*ratio;refresh();}return true;}}return true;}
    boolean motion(MotionEvent e){controllerCursor=true;axisX=dead(e.getAxisValue(MotionEvent.AXIS_X));axisY=dead(e.getAxisValue(MotionEvent.AXIS_Y));lastTime=SystemClock.uptimeMillis();removeCallbacks(cursorTick);postOnAnimation(cursorTick);return true;}
    static float dead(float x){return Math.abs(x)<.18f?0:Math.signum(x)*(Math.abs(x)-.18f)/.82f;}
    final Runnable cursorTick=new Runnable(){public void run(){if(!editing)return;long n=SystemClock.uptimeMillis();float dt=Math.min(.05f,(n-lastTime)/1000f);lastTime=n;cursorX=RuneTouchLayout.clamp(cursorX+axisX*650*dt,0,vw());cursorY=RuneTouchLayout.clamp(cursorY+axisY*650*dt,0,vh());drag(cursorX,cursorY);invalidate();if(axisX!=0||axisY!=0)postOnAnimation(this);}};
    final class Control extends Button {
        final int id;final Paint p=new Paint(3);final Path path=new Path();
        final HashSet<Integer> pointers=new HashSet<>();int owner=-1;float lastX,lastY,stickX,stickY;
        boolean keyHeld,centerTap;long tapDownAt,lastTapUp;float tapX,tapY;
        boolean center(float x,float y){return Math.hypot(x-getWidth()*.5f,y-getHeight()*.5f)<=Math.min(getWidth(),getHeight())*.18f;}
        void beginTap(MotionEvent e,float x,float y){centerTap=center(x,y);tapDownAt=e.getEventTime();tapX=x;tapY=y;if(!centerTap)lastTapUp=0;}
        void moveTap(float x,float y){float slop=Math.max(ViewConfiguration.get(getContext()).getScaledTouchSlop(),Math.min(getWidth(),getHeight())*.035f);if(!center(x,y)||Math.hypot(x-tapX,y-tapY)>slop){centerTap=false;lastTapUp=0;}}
        void endTap(MotionEvent e,float x,float y){
            moveTap(x,y);long now=e.getEventTime();
            if(centerTap&&now-tapDownAt<=220){
                if(lastTapUp>0&&tapDownAt>=lastTapUp&&tapDownAt-lastTapUp<=ViewConfiguration.getDoubleTapTimeout()){lastTapUp=0;cameraJump();}
                else lastTapUp=now;
            }else lastTapUp=0;
            centerTap=false;
        }
        void setKey(boolean down){keyHeld=down;publishKey(CODES[id]);}
        Control(Context c,int id){super(c);this.id=id;setText(RuneTouchLayout.NAMES[id]);setAllCaps(false);setContentDescription(RuneTouchLayout.NAMES[id]);setBackgroundColor(Color.TRANSPARENT);setPadding(0,0,0,0);setMinWidth(0);setMinimumWidth(0);setMinHeight(0);setMinimumHeight(0);setStateListAnimator(null);setFocusable(false);setOnClickListener(v->{if(id==1&&!editing)host.touchMenu();});}
        void release(){setKey(false);centerTap=false;lastTapUp=0;if(owner!=-1){if(id==0)host.touchMove(0,0);else if(id==10)host.touchCamera(0,0);}pointers.clear();owner=-1;stickX=stickY=0;setPressed(false);invalidate();}
        void stick(float x,float y){float r=Math.min(getWidth(),getHeight())*.36f;float sx=(x-getWidth()/2)/r,sy=(y-getHeight()/2)/r;float len=(float)Math.hypot(sx,sy);if(len>1){sx/=len;sy/=len;}stickX=sx;stickY=sy;float mag=(float)Math.hypot(sx,sy),gain=mag<=.12f?0:(mag-.12f)/(.88f*mag);if(id==0)host.touchMove(sx*gain,-sy*gain);else host.touchCamera(sx*gain,-sy*gain);invalidate();}
        @Override public boolean onTouchEvent(MotionEvent e){if(editing)return false;if(!isEnabled()||(!shown&&id!=1))return false;int a=e.getActionMasked(),index=e.getActionIndex(),pid=e.getPointerId(index);
            if(a==MotionEvent.ACTION_DOWN||a==MotionEvent.ACTION_POINTER_DOWN){boolean empty=pointers.isEmpty();pointers.add(pid);setPressed(true);if(empty){owner=pid;lastX=e.getX(index);lastY=e.getY(index);if(id==10)beginTap(e,lastX,lastY);if(id==0||id==10)stick(lastX,lastY);else setKey(true);}else if(id==10){centerTap=false;lastTapUp=0;}}
            else if(a==MotionEvent.ACTION_MOVE){int i=e.findPointerIndex(owner);if(i>=0){float x=e.getX(i),y=e.getY(i);if(id==10)moveTap(x,y);if(id==0||id==10)stick(x,y);else if(id==9)host.touchLook(x-lastX,y-lastY);lastX=x;lastY=y;}}
            else if(a==MotionEvent.ACTION_CANCEL)release();
            else if(a==MotionEvent.ACTION_UP||a==MotionEvent.ACTION_POINTER_UP){if(id==10&&pointers.size()==1)endTap(e,e.getX(index),e.getY(index));pointers.remove(pid);if(pointers.isEmpty()){boolean click=id==1&&a==MotionEvent.ACTION_UP; // Release before Menu clears the whole overlay.
                    setKey(false);if(id==0)host.touchMove(0,0);else if(id==10)host.touchCamera(0,0);owner=-1;stickX=stickY=0;setPressed(false);if(click)performClick();}
                else if(owner==pid){owner=pointers.iterator().next();int i=e.findPointerIndex(owner);lastX=e.getX(i);lastY=e.getY(i);if(id==0||id==10)stick(lastX,lastY);}}
            invalidate();return true;
        }
        void line(Canvas c,float... points){path.reset();path.moveTo(points[0],points[1]);for(int i=2;i<points.length;i+=2)path.lineTo(points[i],points[i+1]);c.drawPath(path,p);}
        void icon(Canvas c){p.setColor(IVORY);p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(4);p.setStrokeCap(Paint.Cap.ROUND);p.setStrokeJoin(Paint.Join.ROUND);
            if(id==4){line(c,-17,20,19,-22,24,-25,23,-15,-13,23);line(c,-19,10,-4,24);line(c,-20,25,-12,16);}
            else if(id==3){line(c,0,-26,23,-17,20,6,12,20,0,28,-12,20,-20,6,-23,-17,0,-26);line(c,0,-20,0,22);}
            else if(id==5){ // Side-on kicking figure, extended boot and planted rear foot.
                p.setStyle(Paint.Style.FILL);c.drawCircle(-14,-23,5.5f,p);
                p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(6);
                line(c,-14,-12,-8,2,-20,24);line(c,-24,26,-12,26);
                line(c,-8,2,7,-6,23,0);line(c,-12,-9,-27,-2);line(c,-11,-9,2,-15);
                path.reset();path.moveTo(19,-4);path.lineTo(28,-3);path.lineTo(32,5);path.lineTo(20,5);path.close();
                p.setStyle(Paint.Style.FILL);c.drawPath(path,p);
            }
            else if(id==6){ // Three ascending tiers; longest at the bottom.
                p.setStrokeWidth(5);line(c,-26,18,26,18);line(c,-17,0,17,0);line(c,-8,-18,8,-18);
            }
            else if(id==7){ // Raised axe gripped in a solid fist; arc shows the throwing motion.
                p.setStrokeWidth(5);line(c,-4,26,8,-23);
                path.reset();path.moveTo(7,-25);path.lineTo(28,-27);path.quadTo(21,-16,29,-5);path.lineTo(5,-12);path.close();
                p.setStyle(Paint.Style.FILL);c.drawPath(path,p);
                path.reset();path.moveTo(-25,26);path.lineTo(-15,12);path.lineTo(-15,6);path.lineTo(-10,1);
                path.lineTo(1,3);path.lineTo(6,9);path.lineTo(3,17);path.lineTo(-6,20);path.lineTo(-13,29);path.close();c.drawPath(path,p);
                p.setStyle(Paint.Style.STROKE);p.setColor(0xff181b1c);p.setStrokeWidth(2.5f);
                line(c,-10,7,2,9);line(c,-11,12,0,14);line(c,-6,3,4,8);
                p.setColor(IVORY);p.setStrokeWidth(3);
                path.reset();path.moveTo(-29,-5);path.quadTo(-25,-24,-10,-29);c.drawPath(path,p);
                line(c,-17,-29,-10,-29,-11,-22);
            }
            else if(id==8){ // Head-first dive: straight trailing legs, torso, arms reaching ahead.
                p.setStyle(Paint.Style.FILL);c.drawCircle(14,0,6,p);
                p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(9);
                line(c,-9,-7,4,6); // torso
                p.setStrokeWidth(5.5f);
                line(c,-12,-8,-30,-29);line(c,-6,-11,-22,-32); // two straight legs, no reversed knees
                line(c,0,4,15,16,29,28);line(c,5,7,20,15,32,24); // arms extended head-first
            }
            else if(id==2){line(c,-17,8,-22,-4,-18,-9,-10,0,-10,-23,-5,-26,-3,-24,-3,-5,-3,-30,3,-31,5,-28,5,-5,5,-25,11,-25,13,-21,13,-4,13,-17,19,-16,20,-10,20,13,13,25,-3,25,-17,8);}
            else if(id==RuneTouchLayout.POWER){ // Rune stone with a bright central sigil.
                line(c,0,-30,23,-9,16,19,0,29,-16,19,-23,-9,0,-30);
                line(c,-10,-8,0,-18,10,-8,0,11,-10,-8);
                line(c,0,11,0,22);
            }
        }
        @Override protected void onDraw(Canvas c){float w=getWidth(),h=getHeight();if(id==9){if(editing){p.setColor(0x18d3af70);p.setStyle(Paint.Style.FILL);c.drawRect(0,0,w,h,p);label(c,"Look area",w/2,h/2,22*unit());}else return;}
            else{path.reset();if(id==1)path.addRoundRect(2,2,w-2,h-2,7*unit(),7*unit(),Path.Direction.CW);else path.addCircle(w/2,h/2,Math.min(w,h)/2-2,Path.Direction.CW);p.setStyle(Paint.Style.FILL);boolean cue=bloodlustCue(id);float pulse=cue?.5f+.5f*(float)Math.sin(SystemClock.uptimeMillis()*Math.PI*2/1100):0;
                p.setColor(cue?Color.argb(220,75+(int)(65*pulse),46+(int)(35*pulse),12):(isPressed()?0xe0473821:0xd0181b1c));c.drawPath(path,p);p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(1.7f*unit());p.setColor(cue?Color.rgb(255,165+(int)(55*pulse),38):(isPressed()?0xffffd58b:GOLD));c.drawPath(path,p);
                c.save();c.clipPath(path);
                if(id==0||id==10){p.setStrokeWidth(2*unit());p.setColor(GOLD);float r=w*.16f;if(isPressed())c.drawCircle(w/2+stickX*w*.22f,h/2+stickY*h*.22f,r,p);label(c,id==0?"Move":"Look",w/2,h*.55f,h*.12f);}
                else if(id==1){p.setStrokeWidth(2*unit());line(c,w*.17f,h*.32f,w*.34f,h*.32f);line(c,w*.17f,h*.5f,w*.34f,h*.5f);line(c,w*.17f,h*.68f,w*.34f,h*.68f);label(c,"Menu",w*.64f,h*.65f,h*.37f);}
                else{c.save();c.translate(w/2,h*.37f);float s=Math.min(w,h)*.0085f;c.scale(s,s);icon(c);c.restore();if(id==RuneTouchLayout.DIVE||id==RuneTouchLayout.POWER){label(c,id==RuneTouchLayout.DIVE?"Crouch /":"Rune",w/2,h*.69f,h*.14f);label(c,id==RuneTouchLayout.DIVE?"Dive":"Power",w/2,h*.84f,h*.14f);}else label(c,cue?(id==6?"Leap · 2×":id==3?"Hold Block":"+ Attack"):RuneTouchLayout.NAMES[id],w/2,h*.77f,h*(cue?.13f:.16f));
                    }c.restore();}
            if(editing){p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(1.5f*unit());p.setColor(selected==id?0xffffd27c:GOLD);p.setPathEffect(new DashPathEffect(new float[]{7*unit(),5*unit()},0));c.drawRect(3,3,w-3,h-3,p);p.setPathEffect(null);if(selected==id){p.setStyle(Paint.Style.FILL);float r=7*unit();for(float x:new float[]{r,w-r})for(float y:new float[]{r,h-r})c.drawRect(x-r,y-r,x+r,y+r,p);}}
        }
        void label(Canvas c,String s,float x,float y,float size){p.setStyle(Paint.Style.FILL);p.setColor(IVORY);p.setTypeface(RuneMenuView.BODY_FONT);p.setTextSize(size);float width=p.measureText(s),available=getWidth()*.74f;if(width>available)p.setTextSize(size*available/width);p.setTextAlign(Paint.Align.CENTER);c.drawText(s,x,y,p);}
    }
}
