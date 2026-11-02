package org.runeclassic.port;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.*;
import android.os.SystemClock;
import android.view.*;
import android.widget.*;
import java.util.Locale;

/** Native-resolution HUD and paused layout editor. The transparent play overlay
 * never consumes touches; editor input is isolated from SDL/gameplay. */
final class RuneHudView extends FrameLayout {
    interface Host { void closeHudEditor(); }
    final Host host;
    final SharedPreferences prefs;
    RuneHudLayout saved,layout;
    final HudCanvas canvas;
    final FrameLayout editor;
    final FrameLayout inspector,toolbar;
    final Button showPanels;
    final TextView selectedLabel,sizeLabel;
    final SeekBar sizeSlider;
    final Button orientation;
    final float[] stats=new float[12];
    String messages="",score="",fpsText="",toast="";
    long toastUntil;
    boolean editing,showFps=true,updating,modern=true;
    boolean previewBackdrop;
    Bitmap previewBackground;
    int selected=0;
    float cursorX=400,cursorY=300,axisX,axisY;
    boolean controllerCursor,grabbing,resizing;
    float startX,startY,startLeft,startTop,startSize;
    long lastCursorTime;
    static final int GOLD=0xffc8a56b,IVORY=0xffeee8db;
    static final int[] COLORS={0xffbd2429,0xffc29a57,0xff329ed6,0xffe88e25,0xff60c6c5};

    RuneHudView(Context c,SharedPreferences prefs,Host host){
        super(c);this.prefs=prefs;this.host=host;saved=RuneHudLayout.load(prefs);layout=saved;
        setClipChildren(false);setFocusableInTouchMode(true);
        canvas=new HudCanvas(c);addView(canvas,new FrameLayout.LayoutParams(-1,-1));
        editor=new FrameLayout(c);editor.setClipChildren(false);addView(editor,new FrameLayout.LayoutParams(1536,1024));
        toolbar=panel(editor,0,0,1536,58);
        text(toolbar,"Edit HUD",28,4,240,50,28);
        text(toolbar,"Drag to move · corner to resize",250,4,530,50,20);
        button(toolbar,"Cancel",1110,4,180,50,()->finish(false));
        button(toolbar,"Save layout",1300,4,210,50,()->finish(true));
        button(toolbar,"Hide panels",850,4,230,50,()->panels(false));
        showPanels=button(editor,"Show editor",1300,950,210,52,()->panels(true));showPanels.setVisibility(GONE);
        inspector=panel(editor,1010,548,500,454);
        selectedLabel=text(inspector,"",24,15,450,40,27);
        button(inspector,"‹ Element",24,66,216,52,()->select((selected+RuneHudLayout.COUNT-1)%RuneHudLayout.COUNT));
        button(inspector,"Element ›",254,66,222,52,()->select((selected+1)%RuneHudLayout.COUNT));
        orientation=button(inspector,"",24,132,452,52,()->{if(selected<5){layout.items[selected].horizontal=!layout.items[selected].horizontal;refresh();}});
        sizeLabel=text(inspector,"",24,198,240,40,22);
        button(inspector,"−",320,192,72,52,()->size(layout.items[selected].size-.05f));
        button(inspector,"+",404,192,72,52,()->size(layout.items[selected].size+.05f));
        sizeSlider=new SeekBar(c);sizeSlider.setMax(150);sizeSlider.setProgressTintList(android.content.res.ColorStateList.valueOf(GOLD));sizeSlider.setThumbTintList(android.content.res.ColorStateList.valueOf(GOLD));place(inspector,sizeSlider,24,246,452,46);
        sizeSlider.setContentDescription("Selected HUD size");
        sizeSlider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
            public void onProgressChanged(SeekBar b,int p,boolean user){if(user&&!updating)size((p+50)/100f);}
            public void onStartTrackingTouch(SeekBar b){} public void onStopTrackingTouch(SeekBar b){}
        });
        button(inspector,"Reset element",24,300,452,50,()->{layout.reset(selected);refresh();});
        button(inspector,"Vertical preset",24,364,216,50,()->{layout.preset(false);refresh();});
        button(inspector,"Horizontal preset",254,364,222,50,()->{layout.preset(true);refresh();});
        text(inspector,"Stick: cursor · hold L3 / A: drag · B: cancel",24,417,460,28,16);
        editor.setVisibility(GONE);
        addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or,ob)->{
            float s=unit();editor.setPivotX(0);editor.setPivotY(0);editor.setScaleX(s);editor.setScaleY(s);
            editor.setTranslationX((getWidth()-1536*s)/2);editor.setTranslationY((getHeight()-1024*s)/2);
        });
    }
    private <T extends View>T place(FrameLayout parent,T v,int x,int y,int w,int h){FrameLayout.LayoutParams p=new FrameLayout.LayoutParams(w,h);p.leftMargin=x;p.topMargin=y;parent.addView(v,p);return v;}
    private FrameLayout panel(FrameLayout parent,int x,int y,int w,int h){FrameLayout p=new FrameLayout(getContext());p.setBackground(new RuneMenuView.Iron(false,true));return place(parent,p,x,y,w,h);}
    private TextView text(FrameLayout parent,String s,int x,int y,int w,int h,int size){TextView v=new TextView(getContext());v.setText(s);v.setTextColor(IVORY);v.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX,size);v.setGravity(Gravity.CENTER_VERTICAL);return place(parent,v,x,y,w,h);}
    private Button button(FrameLayout parent,String s,int x,int y,int w,int h,Runnable action){Button v=new Button(getContext());v.setText(s);v.setAllCaps(false);v.setTextColor(IVORY);v.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX,21);v.setPadding(5,0,5,0);v.setMinHeight(0);v.setMinimumHeight(0);v.setBackground(new RuneMenuView.Iron(false,false));v.setOnClickListener(a->action.run());return place(parent,v,x,y,w,h);}
    float unit(){return Math.max(.1f,Math.min(getWidth()/1536f,getHeight()/1024f));}
    float vw(){return getWidth()/unit();} float vh(){return getHeight()/unit();}
    void begin(boolean titlePreview,Bitmap background){previewBackdrop=titlePreview;previewBackground=background;layout=saved.copy();editing=true;controllerCursor=false;cursorX=Math.min(400,vw()/2);cursorY=Math.min(300,vh()/2);axisX=axisY=0;grabbing=false;editor.setVisibility(VISIBLE);panels(true);setVisibility(VISIBLE);select(0);requestFocus();}
    void panels(boolean show){toolbar.setVisibility(show?VISIBLE:GONE);inspector.setVisibility(show?VISIBLE:GONE);showPanels.setVisibility(show?GONE:VISIBLE);}
    void finish(boolean save){if(save){saved=layout.copy();saved.save(prefs);}layout=saved;editing=false;previewBackdrop=false;previewBackground=null;axisX=axisY=0;grabbing=false;canvas.removeCallbacks(cursorTick);editor.setVisibility(GONE);host.closeHudEditor();canvas.invalidate();}
    void select(int i){selected=i;refresh();}
    void size(float n){layout.items[selected].size=RuneHudLayout.clamp(n,.5f,2f);refresh();}
    void refresh(){updating=true;selectedLabel.setText(RuneHudLayout.NAMES[selected]);orientation.setEnabled(selected<5);orientation.setText(selected<5?(layout.items[selected].horizontal?"Orientation: Horizontal":"Orientation: Vertical"):"Independent size & position");sizeLabel.setText("Size  "+Math.round(layout.items[selected].size*100)+"%");sizeSlider.setProgress(Math.round(layout.items[selected].size*100)-50);updating=false;canvas.invalidate();}
    void update(float[] values,String text,String fps,boolean counter){
        String[] texts=text.split("\u001f",-1);String newScore=texts.length>1?texts[1]:"";text=texts[0];
        boolean dirty=!score.equals(newScore)||!messages.equals(text)||!fpsText.equals(fps)||showFps!=counter;
        for(int i=0;i<12;i++){if(stats[i]!=values[i])dirty=true;stats[i]=values[i];}
        messages=text;score=newScore;fpsText=fps;showFps=counter;
        if(dirty||!toast.isEmpty()){
            if(SystemClock.uptimeMillis()>toastUntil)toast="";
            StringBuilder accessible=new StringBuilder();
            for(int i=0;i<5;i++)if(((int)stats[0]&(1<<i))!=0)accessible.append(RuneHudLayout.NAMES[i]).append(": ").append(Math.round(Math.max(0,stats[1+i*2]))).append(" / ").append(Math.round(stats[2+i*2])).append(". ");
            canvas.setContentDescription(accessible.toString()+messages+(showFps?". "+fpsText:""));canvas.invalidate();
        }
    }
    void notify(String s){toast=s;toastUntil=SystemClock.uptimeMillis()+3500;canvas.invalidate();}
    boolean visible(int i){return editing||((modern||i==6)&&(i<5?(((int)stats[0]&(1<<i))!=0):i==5?(!messages.isEmpty()||!toast.isEmpty()):i==6?showFps:!score.isEmpty()));}
    RectF bounds(int i){float s=layout.scale(i,vw(),vh()),x=layout.left(i,vw(),vh()),y=layout.top(i,vw(),vh());return new RectF(x,y,x+layout.width(i)*s,y+layout.height(i)*s);}
    boolean pick(float x,float y){
        RectF b=bounds(selected);
        if(Math.abs(x-b.right)<26&&Math.abs(y-b.bottom)<26){resizing=true;}
        else {resizing=false;boolean found=false;for(int i=RuneHudLayout.COUNT-1;i>=0;i--)if(bounds(i).contains(x,y)){select(i);found=true;break;}if(!found)return false;}
        b=bounds(selected);startX=x;startY=y;startLeft=b.left;startTop=b.top;startSize=layout.items[selected].size;grabbing=true;return true;
    }
    void drag(float x,float y){if(!grabbing)return;if(resizing){float dx=x-startX,dy=y-startY;float bw=layout.width(selected),bh=layout.height(selected);size(startSize+(dx*bw+dy*bh)/(bw*bw+bh*bh));layout.move(selected,startLeft,startTop,vw(),vh());}else{layout.move(selected,startLeft+x-startX,startTop+y-startY,vw(),vh());canvas.invalidate();}}
    // The game overlay passes through all input until Edit HUD is opened.
    @Override public boolean dispatchTouchEvent(MotionEvent e){if(!editing)return false;controllerCursor=false;return super.dispatchTouchEvent(e);}
    boolean key(KeyEvent e){
        int k=e.getKeyCode();boolean down=e.getAction()==KeyEvent.ACTION_DOWN;
        if(k==KeyEvent.KEYCODE_BUTTON_B||k==KeyEvent.KEYCODE_BACK||k==KeyEvent.KEYCODE_ESCAPE){if(down&&e.getRepeatCount()==0)finish(false);return true;}
        if(k==KeyEvent.KEYCODE_BUTTON_START){if(down&&e.getRepeatCount()==0)finish(true);return true;}
        if(k==KeyEvent.KEYCODE_BUTTON_L1||k==KeyEvent.KEYCODE_BUTTON_R1){if(down)select((selected+(k==KeyEvent.KEYCODE_BUTTON_L1?RuneHudLayout.COUNT-1:1))%RuneHudLayout.COUNT);return true;}
        if(k==KeyEvent.KEYCODE_BUTTON_THUMBL||k==KeyEvent.KEYCODE_BUTTON_A){
            controllerCursor=true;if(down&&e.getRepeatCount()==0){if(!clickEditor(cursorX,cursorY))pick(cursorX,cursorY);}else if(!down){grabbing=false;refresh();}return true;
        }
        if(k>=KeyEvent.KEYCODE_DPAD_UP&&k<=KeyEvent.KEYCODE_DPAD_RIGHT){
            controllerCursor=true;if(down){cursorX+=k==KeyEvent.KEYCODE_DPAD_LEFT?-10:k==KeyEvent.KEYCODE_DPAD_RIGHT?10:0;cursorY+=k==KeyEvent.KEYCODE_DPAD_UP?-10:k==KeyEvent.KEYCODE_DPAD_DOWN?10:0;cursorX=RuneHudLayout.clamp(cursorX,0,vw());cursorY=RuneHudLayout.clamp(cursorY,0,vh());drag(cursorX,cursorY);canvas.invalidate();}return true;
        }
        return true;
    }
    private boolean clickEditor(float x,float y){return clickAt(editor,(x*unit()-editor.getTranslationX())/unit(),(y*unit()-editor.getTranslationY())/unit());}
    private boolean clickAt(ViewGroup group,float x,float y){
        if(group.getVisibility()!=VISIBLE)return false;
        for(int i=group.getChildCount()-1;i>=0;i--){View v=group.getChildAt(i);if(v.getVisibility()!=VISIBLE||x<v.getLeft()||y<v.getTop()||x>v.getRight()||y>v.getBottom())continue;
            if(v instanceof ViewGroup&&clickAt((ViewGroup)v,x-v.getLeft(),y-v.getTop()))return true;
            if(v instanceof Button){if(v.isEnabled())v.performClick();return true;}
            if(v instanceof SeekBar){size(.5f+1.5f*(x-v.getLeft())/v.getWidth());return true;}
        }return false;
    }
    boolean motion(MotionEvent e){controllerCursor=true;axisX=dead(e.getAxisValue(MotionEvent.AXIS_X));axisY=dead(e.getAxisValue(MotionEvent.AXIS_Y));lastCursorTime=SystemClock.uptimeMillis();canvas.removeCallbacks(cursorTick);canvas.postOnAnimation(cursorTick);return true;}
    float dead(float v){return Math.abs(v)<.18f?0:Math.signum(v)*(Math.abs(v)-.18f)/.82f;}
    final Runnable cursorTick=new Runnable(){public void run(){if(!editing)return;long now=SystemClock.uptimeMillis();float dt=Math.min(.05f,(now-lastCursorTime)/1000f);lastCursorTime=now;cursorX=RuneHudLayout.clamp(cursorX+axisX*650*dt,0,vw());cursorY=RuneHudLayout.clamp(cursorY+axisY*650*dt,0,vh());drag(cursorX,cursorY);canvas.invalidate();if(axisX!=0||axisY!=0)canvas.postOnAnimation(this);}};
    void suspend(){axisX=axisY=0;grabbing=false;canvas.removeCallbacks(cursorTick);}

    final class HudCanvas extends View {
        final Paint p=new Paint(3);
        final Path path=new Path();
        int pointer=-1;
        HudCanvas(Context c){super(c);setLayerType(View.LAYER_TYPE_HARDWARE,null);}
        @Override protected void onDraw(Canvas c){
            super.onDraw(c);c.save();c.scale(unit(),unit());
            if(editing&&previewBackdrop){
                c.drawColor(0xff0b1013);
                if(previewBackground!=null){p.setColor(Color.WHITE);p.setStyle(Paint.Style.FILL);c.drawBitmap(previewBackground,null,new RectF(0,0,vw(),vh()),p);}
            }
            if(editing)c.drawColor(0x55000000);
            for(int i=0;i<RuneHudLayout.COUNT;i++)if(visible(i))drawWidget(c,i);
            if(editing){RectF r=bounds(selected);p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(2);p.setColor(0xffffd684);c.drawRect(r,p);p.setStyle(Paint.Style.FILL);p.setColor(GOLD);c.drawRect(r.right-10,r.bottom-10,r.right+10,r.bottom+10,p);}
            c.restore();
        }
        void fill(Canvas c,float x,float y,float w,float h,int color){p.setStyle(Paint.Style.FILL);p.setColor(color);c.drawRect(x,y,x+w,y+h,p);}
        void line(Canvas c,float x,float y,float a,float b,int color,float width){p.setColor(color);p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(width);c.drawLine(x,y,a,b,p);p.setStyle(Paint.Style.FILL);}
        void label(Canvas c,String s,float x,float y,float size,int color,boolean center){p.setStyle(Paint.Style.FILL);p.setColor(color);p.setTextSize(size);p.setTypeface(RuneMenuView.BODY_FONT);p.setTextAlign(center?Paint.Align.CENTER:Paint.Align.LEFT);c.drawText(s,x,y,p);}
        void frame(Canvas c,float w,float h){
            fill(c,0,0,w,h,0xe80d1112);fill(c,2,2,w-4,Math.min(h/3,25),0x332e3435);
            p.setColor(GOLD);p.setStrokeWidth(1.2f);p.setStyle(Paint.Style.STROKE);c.drawRect(1,1,w-1,h-1,p);
            for(int corner=0;corner<4;corner++){c.save();c.translate(corner%2==0?0:w,corner<2?0:h);c.scale(corner%2==0?1:-1,corner<2?1:-1);path.reset();path.moveTo(2,17);path.lineTo(17,2);path.lineTo(23,2);path.lineTo(5,20);path.lineTo(5,11);path.lineTo(13,11);path.lineTo(13,4);c.drawPath(path,p);c.restore();}p.setStyle(Paint.Style.FILL);
        }
        void icon(Canvas c,int i,float x,float y,float s){
            c.save();c.translate(x,y);c.scale(s/40,s/40);p.setColor(GOLD);p.setStrokeWidth(1.3f);p.setStyle(Paint.Style.STROKE);path.reset();path.moveTo(0,-21);path.lineTo(21,0);path.lineTo(0,21);path.lineTo(-21,0);path.close();c.drawPath(path,p);
            p.setColor(i==2?0xff85d5f8:i==3?0xffffbe56:0xffdecb9e);p.setStrokeWidth(2);
            path.reset();
            if(i==0){p.setStyle(Paint.Style.FILL);c.drawOval(-10,-12,10,8,p);c.drawRect(-6,4,6,13,p);p.setColor(0xff101314);c.drawCircle(-4,-2,3,p);c.drawCircle(4,-2,3,p);path.moveTo(0,2);path.lineTo(-2,6);path.lineTo(2,6);path.close();c.drawPath(path,p);for(int t=-4;t<=4;t+=4)line(c,t,8,t,13,0xff34291e,1);}
            else if(i==1){path.moveTo(-11,-10);path.lineTo(0,-14);path.lineTo(11,-10);path.lineTo(9,5);path.lineTo(0,14);path.lineTo(-9,5);path.close();c.drawPath(path,p);c.drawLine(0,-13,0,13,p);}
            else if(i==2){path.moveTo(-4,15);path.lineTo(-4,-15);path.lineTo(8,-7);path.lineTo(-4,0);path.lineTo(8,6);path.lineTo(-4,14);c.drawPath(path,p);}
            else if(i==3){path.moveTo(0,15);path.cubicTo(-18,8,-6,-1,-7,-9);path.lineTo(-1,-4);path.lineTo(3,-16);path.cubicTo(4,-4,17,4,5,14);path.close();p.setStyle(Paint.Style.FILL);c.drawPath(path,p);p.setColor(0xffffe1a4);c.drawOval(-3,2,4,13,p);}
            else {c.drawCircle(0,2,8,p);c.drawCircle(8,-10,3,p);c.drawCircle(-6,-13,2,p);}
            p.setStyle(Paint.Style.FILL);c.restore();
        }
        void meter(Canvas c,float x,float y,float w,float h,float amount,int color,boolean vertical){
            p.setStyle(Paint.Style.FILL);p.setColor(0xff080c0d);c.drawRoundRect(x,y,x+w,y+h,Math.min(w,h)/2,Math.min(w,h)/2,p);
            float ratio=RuneHudLayout.clamp(amount,0,1);if(ratio>0){p.setShader(new LinearGradient(x,y,x+(vertical?w:0),y+(vertical?0:h),new int[]{color,blend(color,1.35f),color},null,Shader.TileMode.CLAMP));
                c.save();c.clipRect(x+2,vertical?y+2+(h-4)*(1-ratio):y+2,vertical?x+w-2:x+2+(w-4)*ratio,y+h-2);c.drawRoundRect(x+2,y+2,x+w-2,y+h-2,4,4,p);c.restore();p.setShader(null);}
            p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(1);p.setColor(GOLD);c.drawRoundRect(x,y,x+w,y+h,Math.min(w,h)/2,Math.min(w,h)/2,p);p.setStyle(Paint.Style.FILL);
        }
        int blend(int c,float n){return Color.rgb(Math.min(255,(int)(Color.red(c)*n)),Math.min(255,(int)(Color.green(c)*n)),Math.min(255,(int)(Color.blue(c)*n)));}
        void drawWidget(Canvas c,int i){
            RectF b=bounds(i);float scale=layout.scale(i,vw(),vh()),w=layout.width(i),h=layout.height(i);c.save();c.translate(b.left,b.top);c.scale(scale,scale);
            if(i<5){frame(c,w,h);boolean horizontal=layout.items[i].horizontal;
                float value=stats[1+i*2],max=stats[2+i*2];
                if(editing&&(((int)stats[0]&(1<<i))==0)){float[] samples={72,41,40,35,70};float[] maxima={100,70,100,100,100};value=samples[i];max=maxima[i];}
                value=Math.max(0,value);max=Math.max(1,max);String n=i>=3?Math.round(value/max*100)+"%":Math.round(value)+" / "+Math.round(max);
                if(horizontal){icon(c,i,26,25,36);label(c,RuneHudLayout.NAMES[i],57,18,18,IVORY,false);meter(c,57,26,w-145,14,value/max,COLORS[i],false);label(c,n,w-44,35,16,IVORY,true);}
                else {icon(c,i,w/2,29,36);label(c,RuneHudLayout.NAMES[i],w/2,62,i==2?13:17,IVORY,true);meter(c,w/2-10,76,20,h-114,value/max,COLORS[i],true);label(c,n,w/2,h-15,14,IVORY,true);}
            } else if(i==5){
                String m=!toast.isEmpty()?toast:messages;if(editing&&m.isEmpty())m="Checkpoint reached";
                // Wrap long messages without truncation; height expands upward if needed.
                p.setTextSize(20);p.setTypeface(RuneMenuView.BODY_FONT);java.util.ArrayList<String> lines=new java.util.ArrayList<>();
                for(String paragraph:m.split("\n")){String rest=paragraph;while(!rest.isEmpty()){int count=p.breakText(rest,true,w-32,null);count=Math.max(1,count);if(count<rest.length()){int space=rest.lastIndexOf(' ',count);if(space>0)count=space;}lines.add(rest.substring(0,count));rest=rest.substring(count).trim();}}
                float mh=Math.max(44,lines.size()*25+18);float available=(vh()-b.top)/scale-10;float fit=Math.min(1,available/mh);c.scale(1,fit);frame(c,w,mh);for(int j=0;j<lines.size();j++)label(c,lines.get(j),w/2,30+j*25,20,IVORY,true);
            } else if(i==7){frame(c,w,h);label(c,"Score  "+(score.isEmpty()?"0":score),w/2,22,17,IVORY,true);
            } else {
                frame(c,w,h);String text=fpsText.isEmpty()?"FPS — · Native 100% · 4× MSAA":fpsText;
                p.setTypeface(RuneMenuView.BODY_FONT);p.setTextSize(17);
                float size=17*Math.min(1,(w-28)/Math.max(1,p.measureText(text)));
                label(c,text,w/2,22,size,IVORY,true);
            }
            c.restore();
        }
        @Override public boolean onTouchEvent(MotionEvent e){
            if(!editing)return false;
            int action=e.getActionMasked();
            if(action==MotionEvent.ACTION_DOWN){pointer=e.getPointerId(0);pick(e.getX()/unit(),e.getY()/unit());return true;}
            if(action==MotionEvent.ACTION_MOVE){int i=e.findPointerIndex(pointer);if(i>=0)drag(e.getX(i)/unit(),e.getY(i)/unit());return true;}
            if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_CANCEL||(action==MotionEvent.ACTION_POINTER_UP&&e.getPointerId(e.getActionIndex())==pointer)){pointer=-1;grabbing=false;refresh();return true;}return true;
        }
    }
    @Override protected void dispatchDraw(Canvas c){super.dispatchDraw(c);if(editing&&controllerCursor){float u=unit();Paint p=new Paint(3);p.setColor(0xfff5dab0);p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(2*u);c.drawCircle(cursorX*u,cursorY*u,9*u,p);c.drawLine((cursorX-15)*u,cursorY*u,(cursorX+15)*u,cursorY*u,p);c.drawLine(cursorX*u,(cursorY-15)*u,cursorX*u,(cursorY+15)*u,p);}}
}
