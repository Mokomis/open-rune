package org.runeclassic.port;

import android.content.Context;
import android.graphics.*;
import android.graphics.drawable.Drawable;
import android.content.res.ColorStateList;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.*;

/** Native Android widgets on the approved 1536×1024 composition. All live
 * hit targets, focus, sliders and accessibility bounds transform with the art. */
final class RuneMenuView extends FrameLayout {
    static final Typeface BODY_FONT=Typeface.create("sans-serif",Typeface.NORMAL);
    private TextView textureInstallLabel;

    void updateTextureInstallStatus() {
        if (textureInstallLabel == null) return;
        String status = host.textureInstallStatus();
        String actual = MainActivity.nativeTexturePackStatus();
        textureInstallLabel.setText(status.isEmpty() ? actual : actual + "\n" + status);
    }

    interface Host {
        RuneSettings settings();
        String characterRoster();
        int characterPreview(int character,int skin,int[] pixels,float yaw);
        void characterPreviewRotation(int character,int skin,float yaw);
        void resume();
        boolean session(int action,int slot);
        String metadata();
        String controllerStatus();
        String rumbleStatus();
        String scalingStatus();
        String timingStatus();
        String cameraStatus();
        String supportedRefreshRates();
        boolean refreshSupported(int rate);
        void testRumble();
        boolean ready();
        boolean dead();
        boolean practiceArena();
        void exit();
        void editHud();
        void editTouch();
        boolean multiplayer();
        boolean multiplayerWaiting();
        String multiplayerStatus();
        void chooseInvitation();
        String textureInstallStatus();
        void selectTextureArtwork(int quality);
        void leaveMultiplayer();
        void respawnMultiplayer();
        RuneHosting hosting();
        RuneDiscovery discovery();
        void joinDiscovered(String key);
        boolean hasInvitation();
        void rejoinMultiplayer();
        void saveInvitation(int slot);
    }
    static final int INK=0xff101314, IVORY=0xffeee8db, MUTED=0xffaaa9a3, GOLD=0xffedbd68;
    static final String[] TABS={"Video","Audio","Controls","Interface","Gameplay"};
    final ArrayDeque<String> history=new ArrayDeque<>();
    final HashMap<String,String> rememberedFocus=new HashMap<>();
    final HashMap<String,int[]> rememberedScroll=new HashMap<>();
    String renderedPage="";
    boolean returning=false;
    TextView settingHelp;
    final Host host;
    final RuneSettings settings;
    final FrameLayout scene;
    final Bitmap art,settingsArt,logoMark;
    FrameLayout page,modal;
    String screen="pause",lastSettings="Video";
    boolean titleMode=false;
    String notice="";
    TextView status;
    final RuneHostConfig hostConfig=new RuneHostConfig();
    String hostAddress="";
    String hostFocus="";
    int invitationSlot=2;
    final ArrayList<Runnable> refreshers=new ArrayList<>();
    final HashMap<View,Integer> hiddenAccessibility=new HashMap<>();

    RuneMenuView(Context context,Host host) {
        super(context); this.host=host;settings=host.settings();
        hostConfig.laws=settings.prefs.getInt("laws.host",RuneBattleLaws.ALL)&RuneBattleLaws.ALL;hostConfig.normalBloodlust=(hostConfig.laws&RuneBattleLaws.NORMAL)!=0;hostConfig.physics=Math.max(0,Math.min(2,settings.prefs.getInt("laws.hostPhysics",RuneSettings.PHYSICS_DEFAULT)));
        Bitmap bitmap=null;
        try(InputStream in=context.getAssets().open("menu-background.png")) { bitmap=BitmapFactory.decodeStream(in); }
        catch(IOException ignored) {}
        art=bitmap;
        bitmap=null;
        try(InputStream in=context.getAssets().open("settings-background.png")){bitmap=BitmapFactory.decodeStream(in);}
        catch(IOException ignored){}
        settingsArt=bitmap;
        logoMark=BitmapFactory.decodeResource(getResources(),getResources().getIdentifier("open_rune_mark","drawable",context.getPackageName()));
        setBackgroundColor(0xff080b0c);setClickable(true);
        scene=new FrameLayout(context);scene.setClipChildren(false);
        addView(scene,new FrameLayout.LayoutParams(1536,1024));
        addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or,ob)-> {
            float scale=Math.min((r-l)/1536f,(b-t)/1024f);
            scene.setPivotX(0);scene.setPivotY(0);scene.setScaleX(scale);scene.setScaleY(scale);
            scene.setTranslationX(((r-l)-1536*scale)/2);scene.setTranslationY(((b-t)-1024*scale)/2);
        });
        setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);
    }
    void open(boolean title) { RuneUpdates.check(getContext(),false);titleMode=title;history.clear();renderedPage="";showHome(); }
    void setNotice(String text) { notice=text; refresh(); }
    void refresh() {
        for(Runnable update:new ArrayList<>(refreshers)) update.run();
        if(status!=null) status.setText(!notice.isEmpty()?notice:settings.comparing?"Original visual preview · saved choices retained":screen.equals("hostRules")?"Applies to the new hosted match":screen.endsWith(":campaign")?"New campaign only · saves retain their difficulty":screen.endsWith(":combat")||screen.endsWith(":enemies")?(host.multiplayer()?"Host-controlled during multiplayer":"Campaign preference · multiplayer uses host rules"):host.ready()?(screen.equals("death")||screen.equals("multiplayer")||screen.equals("host")||screen.equals("maps")||screen.equals("invitations")?"":"Changes apply immediately"):"Preparing the world…");
    }
    void back() {
        if(modal!=null) {
            page.removeView(modal);modal=null;
            for(Map.Entry<View,Integer> e:hiddenAccessibility.entrySet())e.getKey().setImportantForAccessibility(e.getValue());
            hiddenAccessibility.clear();return;
        }
        if(screen.equals("death"))return;
        if(screen.equals("maps")&&hostConfig.maps.isEmpty()){notice="Choose at least one map.";refresh();return;}
        if(screen.equals("findgames")){RuneDiscovery d=host.discovery();if(d!=null&&d.busy())d.command("cancel",null);}
        if(!history.isEmpty()) {
            String destination=history.pop();returning=true;
            try{restorePage(destination);}finally{returning=false;}
        } else if(screen.equals("pause")||screen.equals("title")){if(!titleMode)host.resume();}
        else showHome();
    }
    private void restorePage(String key){
        if(key.startsWith("settings:")){String[] parts=key.split(":");if(parts.length==3)showSection(parts[1],parts[2]);else showSettings(parts[1]);return;}
        if(key.startsWith("notices:")){showNotices(key.substring(8));return;}
        if(key.startsWith("notice:")){showNoticeText(key.substring(7));return;}
        switch(key){
        case "campaignLaws":showBattleLaws("campaign");break;case "newLaws":showBattleLaws("new");break;case "hostLaws":showBattleLaws("host");break;case "matchLaws":showBattleLaws("match");break;
        case "multiplayer":showMultiplayer();break;case "findgames":showFindGames(false);break;
        case "host":showHostSetup();break;case "hostRules":showHostRules();break;case "advancedJoin":showAdvancedJoin();break;
        case "maps":showHostMaps();break;case "invitations":showInvitations();break;
        case "characters":showCharacters(false);break;case "campaignCharacters":showCharacters(true);break;
        case "save":showSaves(true);break;case "load":showSaves(false);break;
        case "developer":showDeveloper();break;case "about":showAbout();break;
        default:showHome();break;}
    }
    private String focusKey(View v){if(v==null)return "";if(v.getTag() instanceof String&&((String)v.getTag()).startsWith("law:"))return (String)v.getTag();if(v.getContentDescription()!=null)return v.getContentDescription().toString();return v instanceof TextView?((TextView)v).getText().toString():"";}
    private void scrollViews(View v,ArrayList<View> list){if(v instanceof ScrollView||v instanceof HorizontalScrollView)list.add(v);if(v instanceof ViewGroup)for(int i=0;i<((ViewGroup)v).getChildCount();i++)scrollViews(((ViewGroup)v).getChildAt(i),list);}
    private void restoreFocus(View v,String key){if(!key.isEmpty()&&key.equals(focusKey(v))&&v.isFocusable()){v.requestFocusFromTouch();return;}if(v instanceof ViewGroup)for(int i=0;i<((ViewGroup)v).getChildCount();i++)restoreFocus(((ViewGroup)v).getChildAt(i),key);}
    private void selectCategory(String tab){while(!history.isEmpty()&&history.peek().startsWith("settings:"))history.pop();boolean old=returning;returning=true;try{showSettings(tab);}finally{returning=old;}}
    @Override public View focusSearch(View focused,int direction) {
        if(modal!=null) {
            View next=FocusFinder.getInstance().findNextFocus(modal,focused,direction);
            return next!=null?next:focused;
        }
        return super.focusSearch(focused,direction);
    }
    // SDL can reclaim window focus on startup/resume. Route menu input within
    // the visible page instead of sending synthetic keys back to that surface.
    private View focusFirstControl(View view) {
        if(!view.isShown()||!view.isEnabled())return null;
        if(view instanceof ViewGroup) {
            ViewGroup group=(ViewGroup)view;
            for(int i=0;i<group.getChildCount();i++) {
                View first=focusFirstControl(group.getChildAt(i));
                if(first!=null)return first;
            }
        } else if(view.isFocusable()&&view.requestFocusFromTouch())return view;
        return null;
    }
    boolean controllerKey(KeyEvent event) {
        FrameLayout root=modal!=null?modal:page;
        if(root==null)return true;
        View focused=root.findFocus();
        boolean hadFocus=focused!=null&&focused.isEnabled()&&!(focused instanceof android.view.ViewGroup);
        if(!hadFocus)focused=focusFirstControl(root);
        if(focused==null)return true;
        int key=event.getKeyCode(),direction=0;
        if(key==KeyEvent.KEYCODE_DPAD_UP)direction=View.FOCUS_UP;
        if(key==KeyEvent.KEYCODE_DPAD_DOWN)direction=View.FOCUS_DOWN;
        if(key==KeyEvent.KEYCODE_DPAD_LEFT)direction=View.FOCUS_LEFT;
        if(key==KeyEvent.KEYCODE_DPAD_RIGHT)direction=View.FOCUS_RIGHT;
        if(key==KeyEvent.KEYCODE_TAB)direction=event.isShiftPressed()?View.FOCUS_BACKWARD:View.FOCUS_FORWARD;
        if(direction!=0) {
            if(event.getAction()==KeyEvent.ACTION_DOWN&&hadFocus) {
                // Sliders own left/right; other widgets use spatial navigation.
                if(focused instanceof SeekBar&&key!=KeyEvent.KEYCODE_TAB&&focused.dispatchKeyEvent(event))return true;
                View next=FocusFinder.getInstance().findNextFocus(root,focused,direction);
                if(next!=null)next.requestFocusFromTouch();
            }
            return true;
        }
        if(key==KeyEvent.KEYCODE_BUTTON_A||key==KeyEvent.KEYCODE_DPAD_CENTER||key==KeyEvent.KEYCODE_ENTER) {
            KeyEvent confirm=new KeyEvent(event.getDownTime(),event.getEventTime(),event.getAction(),KeyEvent.KEYCODE_ENTER,event.getRepeatCount());
            focused.dispatchKeyEvent(confirm);
        }
        return true;
    }
    private <T extends View> T place(FrameLayout parent,T view,int x,int y,int w,int h) {
        FrameLayout.LayoutParams p=new FrameLayout.LayoutParams(w,h);p.leftMargin=x;p.topMargin=y;
        parent.addView(view,p);return view;
    }
    private TextView label(FrameLayout parent,String text,int x,int y,int w,int h,int size,int color,boolean serif) {
        TextView v=new TextView(getContext());v.setText(text);v.setTextColor(color);
        v.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX,size);v.setGravity(Gravity.CENTER_VERTICAL);
        v.setFontFeatureSettings("kern");v.setIncludeFontPadding(false);
        v.setTypeface(serif?Typeface.create("serif",Typeface.NORMAL):BODY_FONT);
        return place(parent,v,x,y,w,h);
    }
    private Button button(FrameLayout parent,String text,int x,int y,int w,int h,boolean gold,Runnable action) {
        Button v=new Button(getContext());v.setText(text);v.setAllCaps(false);v.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX,25);
        v.setTextColor(IVORY);v.setTypeface(BODY_FONT);
        v.setPadding(12,0,12,0);v.setMinHeight(0);v.setMinimumHeight(0);v.setMinWidth(0);v.setMinimumWidth(0);
        v.setStateListAnimator(null);v.setBackground(new Iron(false,gold));v.setOnClickListener(view->{view.requestFocusFromTouch();action.run();});
        v.setOnFocusChangeListener((view,focus)-> {
            // Some shortcuts use transparent backgrounds; keep their focus
            // visible too, without changing the normal menu artwork.
            android.graphics.drawable.GradientDrawable ring=new android.graphics.drawable.GradientDrawable();
            ring.setColor(Color.TRANSPARENT);ring.setStroke(2,GOLD);
            view.setForeground(focus?ring:null);view.invalidate();
            if(focus&&view.getTag() instanceof String&&settings.items.containsKey((String)view.getTag()))updateHelp((String)view.getTag());
        });
        return place(parent,v,x,y,w,h);
    }
    private FrameLayout panel(int x,int y,int w,int h,String title) {
        FrameLayout p=place(page,new FrameLayout(getContext()),x,y,w,h);p.setBackground(new Iron(false,true));
        TextView heading=label(p,title,40,24,w-80,54,27,0xffc1b8a7,true);heading.setLetterSpacing(.15f);
        View rule=new View(getContext());rule.setBackgroundColor(0x554e504c);place(p,rule,36,80,w-72,1);return p;
    }
    private void base(String heading) {
        if(page!=null&&!renderedPage.isEmpty()){
            rememberedFocus.put(renderedPage,focusKey(page.findFocus()));ArrayList<View> scrollers=new ArrayList<>();scrollViews(page,scrollers);int[] offsets=new int[scrollers.size()*2];for(int i=0;i<scrollers.size();i++){offsets[i*2]=scrollers.get(i).getScrollX();offsets[i*2+1]=scrollers.get(i).getScrollY();}rememberedScroll.put(renderedPage,offsets);
        }
        if(screen.equals("title")||screen.equals("pause"))history.clear();
        else if(!returning&&!renderedPage.isEmpty()&&!screen.equals(renderedPage)){
            // Returning to an existing ancestor must not create a two-page cycle.
            if(history.contains(screen)){while(!history.isEmpty()&&!history.peek().equals(screen))history.pop();if(!history.isEmpty())history.pop();}
            else history.push(renderedPage);
        }
        renderedPage=screen;settingHelp=null;
        refreshers.clear();hiddenAccessibility.clear();status=null;modal=null;
        scene.removeAllViews();
        page=new FrameLayout(getContext());place(scene,page,0,0,1536,1024);
        place(page,new Backdrop(getContext()),0,0,1536,1024);
        place(page,new Logo(getContext(),logoMark),heading.isEmpty()?112:136,heading.isEmpty()?48:28,heading.isEmpty()?390:240,heading.isEmpty()?110:68);
        final String builtPage=screen;
        page.post(()->{if(!screen.equals(builtPage))return;ArrayList<View> scrollers=new ArrayList<>();scrollViews(page,scrollers);int[] offsets=rememberedScroll.get(builtPage);if(offsets!=null)for(int i=0;i<scrollers.size()&&i*2+1<offsets.length;i++)scrollers.get(i).scrollTo(offsets[i*2],offsets[i*2+1]);restoreFocus(page,rememberedFocus.getOrDefault(builtPage,""));});
        if(!heading.isEmpty()) {
            button(page,"‹",55,20,58,64,false,()->back()).setBackgroundColor(Color.TRANSPARENT);
            label(page,heading,86,104,1180,70,44,IVORY,true);
            button(page,"×  Close",1355,20,135,64,false,()->{ if(titleMode)showHome();else host.resume(); }).setBackgroundColor(Color.TRANSPARENT);
        }
    }
    private void footer(boolean settingsPage) {
        FrameLayout f=place(page,new FrameLayout(getContext()),0,906,1536,118);f.setBackgroundColor(0xee0b1011);
        status=label(f,notice,62,28,830,60,21,MUTED,false);
        String[] keys=keysFor(screen);
        if(settingsPage&&keys.length>0)button(f,"Reset this section",955,24,300,58,false,()->confirm("Reset this section?","Only the controls in this section will return to their defaults. Other sections and layouts are kept.","Reset",()->{for(String key:keys)if(!hostControlled(key))settings.set(key,settings.items.get(key).defaultValue);settings.comparing=false;host.settings().listener.changed("compare");restorePage(screen);}));
        if(settingsPage&&screen.endsWith(":physics")&&!host.multiplayer())button(f,"Physics playground",62,24,480,58,false,()->confirm("Enter physics playground?","This replaces the live scene. Campaign saves stay separate. Use lifts a prop or intact corpse; Attack or Throw launches it.","Enter",()->{if(settings.get("physics")==0)settings.set("physics",RuneSettings.PHYSICS_DEFAULT);runSession(9,0);}));
        button(f,"Back",1285,24,220,58,true,this::back);
    }
    private String[] keysFor(String key){return key.startsWith("settings:")?RuneMenuSections.keys(key.substring(key.lastIndexOf(':')+1)):new String[0];}
    void showHome() {
        if(host.multiplayer()){showMultiplayer();return;}
        if(!titleMode&&host.dead()){showDeath();return;}
        screen=titleMode?"title":"pause";base("");
        if(!titleMode)label(page,"Paused",112,190,550,92,64,IVORY,true);
        TextView chapter=label(page,chapter(),1055,38,420,55,23,0xffbab6ac,true);chapter.setGravity(Gravity.RIGHT|Gravity.CENTER_VERTICAL);
        String[] names=titleMode?new String[]{"Continue","New campaign","Load game","Settings","Multiplayer","Quit"}:new String[]{"Resume","Save game","Load game","Settings","Return to title"};
        int menuTop=titleMode?240:300;
        for(int i=0;i<names.length;i++) {
            final int index=i;
            Button b=button(page,names[i],76,menuTop+i*96,474,86,false,()-> {
                if(titleMode) {
                    if(index==0) { if(hasSession) host.resume();else showSaves(false); }
                    if(index==1){showBattleLaws("new");}
                    if(index==2)showSaves(false);
                    if(index==3)showSettings(lastSettings);
                    if(index==4)showMultiplayer();
                    if(index==5)confirm("Leave Open Rune?","Save before quitting to keep your current progress.","Quit",()->host.exit());
                } else {
                    if(index==0)host.resume();
                    if(index==1)showSaves(true);
                    if(index==2)showSaves(false);
                    if(index==3)showSettings(lastSettings);
                    if(index==4) {titleMode=true;showHome();}
                }
            });
            b.setBackground(new Iron(false,false));
            label(page,"›",508,menuTop+i*96,32,86,36,MUTED,true);
            b.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX,36);b.setTypeface(Typeface.create("serif",Typeface.NORMAL));
            b.setGravity(Gravity.CENTER_VERTICAL|Gravity.LEFT);b.setPadding(38,0,12,0);
            if(i<3)refreshers.add(()->b.setEnabled(host.ready()));
        }
        FrameLayout f=place(page,new FrameLayout(getContext()),0,938,1536,86);f.setBackgroundColor(0xdc080d0e);
        View rule=new View(getContext());rule.setBackgroundColor(0xff78674e);place(f,rule,0,0,1536,1);
        String[] quick={"Video","Audio","Controls"};int x=52;
        for(String tab:quick) {
            Button shortcut=button(f,tab,x,12,145,60,false,()->showSettings(tab));
            shortcut.setBackgroundColor(Color.TRANSPARENT);shortcut.setTypeface(Typeface.create("serif",Typeface.NORMAL));
            View separator=new View(getContext());separator.setBackgroundColor(0xff685942);place(f,separator,x-6,25,1,28);
            x+=155;
        }

        Button updateAvailable=button(f,"Update available",800,12,350,60,false,()->showSection("Support","updates"));
        refreshers.add(()->updateAvailable.setVisibility(RuneUpdates.current.available?View.VISIBLE:View.GONE));
        button(f,"About & support",1220,12,260,60,false,()->showAbout()).setBackgroundColor(Color.TRANSPARENT);
        refresh();
    }
    private void hostBotControls(int x,int y,int width){
        RuneHosting hosting=host.hosting();int half=(width-16)/2;
        Button add=button(page,"Add bot ("+hosting.bots+")",x,y,half,64,false,()->{try{hosting.changeBot(true);}catch(RuntimeException e){setNotice(e.getMessage());}});
        Button remove=button(page,"Remove bot",x+half+16,y,half,64,false,()->{try{hosting.changeBot(false);}catch(RuntimeException e){setNotice(e.getMessage());}});
        refreshers.add(()->{add.setText("Add bot ("+hosting.bots+")");add.setEnabled(hosting.ready()&&hosting.bots<hosting.players-1);remove.setEnabled(hosting.ready()&&hosting.bots>0);});
    }
    void playerProfile(Runnable next){
        FrameLayout card=modal(settings.playerName().isEmpty()?"Choose your player name":"Multiplayer profile","Your name is saved between matches. You can change it here at any time.",540);
        EditText field=new EditText(getContext());field.setSingleLine(true);field.setTextColor(IVORY);field.setTextSize(26);field.setText(settings.playerName());field.setSelectAllOnFocus(true);field.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(24)});place(card,field,42,195,730,70);
        final Button[] visible={null};visible[0]=button(card,settings.get("playerNames")!=0?"Player names: On":"Player names: Off · team markers stay",42,285,730,64,false,()->{settings.set("playerNames",1-settings.get("playerNames"));visible[0].setText(settings.get("playerNames")!=0?"Player names: On":"Player names: Off · team markers stay");});

        TextView error=label(card,"",42,360,730,52,20,GOLD,false);
        Runnable save=()->{try{settings.playerName(field.getText().toString());((android.view.inputmethod.InputMethodManager)getContext().getSystemService(Context.INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(field.getWindowToken(),0);back();if(next!=null)next.run();else showMultiplayer();}catch(IllegalArgumentException e){error.setText(e.getMessage());}};
        button(card,"Save",430,440,342,64,true,save);button(card,"Cancel",42,440,340,64,false,this::back);
        field.setOnEditorActionListener((v,action,event)->{if(action==android.view.inputmethod.EditorInfo.IME_ACTION_DONE){save.run();return true;}return false;});
        field.requestFocus();field.post(()->((android.view.inputmethod.InputMethodManager)getContext().getSystemService(Context.INPUT_METHOD_SERVICE)).showSoftInput(field,android.view.inputmethod.InputMethodManager.SHOW_IMPLICIT));
    }
    void showMultiplayer() {
        screen="multiplayer";base("");
        label(page,"Multiplayer",112,190,1100,92,64,IVORY,true);
        button(page,"Player profile",1080,200,350,64,false,()->playerProfile(null));
        RuneHosting hosting=host.hosting();
        if(host.multiplayer()) {
            ScrollView scores=place(page,new ScrollView(getContext()),740,290,700,500);
            TextView state=new TextView(getContext());state.setTextColor(IVORY);state.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX,27);state.setTypeface(BODY_FONT);
            state.setText(host.multiplayerStatus());scores.addView(state,new ScrollView.LayoutParams(-1,-2));
            refreshers.add(()->state.setText(host.multiplayerStatus()));
            Button resume=button(page,host.dead()?"Respawn":"Resume match",112,310,590,82,true,()->{if(host.dead())host.respawnMultiplayer();else host.resume();});
            refreshers.add(()->{resume.setText(host.multiplayerWaiting()?"Waiting for connection…":host.dead()?"Respawn":"Resume match");resume.setEnabled(!host.multiplayerWaiting()&&host.ready());});
            button(page,"Settings",112,410,590,82,false,()->showSettings(lastSettings));
            if(hosting.ready())button(page,"Invite players",112,510,590,82,false,()->showInvitations());
            button(page,hosting.active()?"Stop hosted match":"Leave match",112,610,590,82,false,()->confirm("Leave this match?",hosting.active()?"You are the host. This ends the match for everyone.":"You will disconnect. Other players can keep playing.","Leave",()->host.leaveMultiplayer()));
            if(hosting.ready())hostBotControls(112,710,590);
            label(page,"The match continues while this menu is open.",112,785,590,40,22,MUTED,false);
        } else if(hosting.active()) {
            TextView state=label(page,hosting.message,112,300,1250,90,29,IVORY,false);refreshers.add(()->state.setText(hosting.message));
            label(page,(hosting.discoverable?"Public match":"Hosting on "+hosting.address)+" · "+hosting.players+" player slots",112,390,1250,70,27,MUTED,false);
            if(hosting.ready()){
                button(page,"Join hosted match",112,490,590,82,true,()->hosting.rejoin());
                button(page,"Invite players",740,490,590,82,false,()->showInvitations());
            }
            button(page,"Stop hosting",112,610,590,82,false,()->confirm("Stop hosting?","Connected players will leave this match.","Stop",()->hosting.stop()));
            button(page,"Back",112,715,590,82,false,this::back);
            if(hosting.ready())hostBotControls(740,610,590);
        } else {
            label(page,"Deathmatch · Team Deathmatch · Arena · HeadBall",112,295,1300,72,30,IVORY,false);
            label(page,"Find a public game, host your own, or use a private invitation.\nPlayers need matching Open Rune builds and game files.",112,370,1250,100,26,MUTED,false);
            Button create=button(page,"Host game",740,500,590,82,false,()->{hostConfig.normalBloodlust=(hostConfig.laws&RuneBattleLaws.NORMAL)!=0;showHostSetup();});
            Button join=button(page,"Advanced joining",112,710,590,82,false,this::showAdvancedJoin);
            Button rejoin=button(page,"Rejoin last match",112,605,590,82,false,()->{if(hasSession)confirm("Rejoin this match?","Unsaved campaign progress will be replaced. Existing saves are kept.","Join",()->host.rejoinMultiplayer());else host.rejoinMultiplayer();});
            refreshers.add(()->{join.setEnabled(host.ready());rejoin.setEnabled(host.hasInvitation()&&host.ready());});
            Button find=button(page,"Find games",112,500,590,82,true,()->showFindGames(true));
            // Discovery is independent of the initial campaign world load.
            // Browse immediately; the eventual game join still uses engine readiness.
            button(page,"Back",112,825,300,64,false,this::back);
            Button characters=button(page,"Character & skin",740,605,590,82,false,()->showCharacters(false));
            refreshers.add(()->characters.setEnabled(host.ready()));
            // A previous local host failure must not replace the current remote-match result.
            if(hosting.state.equals("error")&&(notice==null||notice.isEmpty()))notice=hosting.message;
        }
        button(page,"Connection settings",740,825,590,64,false,()->showSettings("Connection"));
        status=label(page,notice,112,925,1290,55,22,MUTED,false);refresh();
    }
    void showCharacters(boolean campaign) {
        screen=campaign?"campaignCharacters":"characters";base("");
        label(page,campaign?"Campaign character":"Multiplayer character",112,185,1300,85,56,IVORY,true);
        label(page,"Tap a character for a closer look and skins, or use Select below its card.",112,278,1300,65,26,MUTED,false);
        String[] roster=host.characterRoster().trim().split("\n");
        HorizontalScrollView carousel=place(page,new HorizontalScrollView(getContext()),112,350,1290,362);
        carousel.setHorizontalScrollBarEnabled(false);carousel.setFillViewport(false);
        LinearLayout cards=new LinearLayout(getContext());cards.setOrientation(LinearLayout.HORIZONTAL);
        carousel.addView(cards,new HorizontalScrollView.LayoutParams(-2,-1));
        ArrayList<CharacterCard> choices=new ArrayList<>();ArrayList<Button> quickChoices=new ArrayList<>();
        TextView selection=label(page,"",112,785,1290,55,28,IVORY,false);
        Runnable update=()->{
            int packed=settings.characterChoice(campaign),selected=packed<0?-1:packed&255,skin=packed<0?0:packed>>8;
            selection.setText(selected<0?"Original story character":"Selected: "+roster[selected].split("\\|",-1)[1]+" · "+characterSkinName(skin));
            for(int i=0;i<choices.size();++i){CharacterCard card=choices.get(i);boolean active=i==selected;card.previewSkin=active?skin:0;
                card.setSelected(active);card.setTextColor(active?GOLD:IVORY);
                String[] entry=roster[i].split("\\|",-1);int skins=Integer.parseInt(entry[2]);
                card.setContentDescription(entry[1]+", enlarge preview"+(skins>1?", "+skins+" skins":"")+(active?", selected":""));
                Button quick=quickChoices.get(i);quick.setText(active?"Selected ✓":"Select");quick.setContentDescription("Select "+entry[1]);quick.setSelected(active);
            }
        };
        for(int i=0;i<roster.length;++i) {
            final int index=i;String[] entry=roster[i].split("\\|",-1);int skins=Integer.parseInt(entry[2]);
            FrameLayout column=new FrameLayout(getContext());
            LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(312,356);lp.rightMargin=14;cards.addView(column,lp);
            CharacterCard card=place(column,new CharacterCard(index),0,0,312,280);card.setAllCaps(false);card.setTypeface(Typeface.create("serif",Typeface.BOLD));
            card.setText(entry[1]);card.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX,29);card.setPadding(12,8,12,12);card.setGravity(Gravity.BOTTOM|Gravity.CENTER_HORIZONTAL);
            card.setBackground(new CharacterCardBackground());card.setFocusable(true);
            card.setOnClickListener(v->showCharacterDetail(campaign,index,entry[1],skins,card,update));
            card.setOnFocusChangeListener((v,focused)->{if(focused){carousel.smoothScrollTo(Math.max(0,index*326-326),0);card.invalidate();}});
            if(skins>1){TextView badge=label(column,skins+" skins",196,12,104,36,21,GOLD,true);badge.setBackgroundColor(0xee101617);badge.setGravity(Gravity.CENTER);badge.setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);}
            Button quick=button(column,"Select",0,292,312,60,false,()->{settings.characterChoice(campaign,index,card.previewSkin);update.run();});
            quick.setOnFocusChangeListener((v,focused)->{if(focused)carousel.smoothScrollTo(Math.max(0,index*326-326),0);});
            choices.add(card);quickChoices.add(quick);
        }
        button(page,"‹ Previous",112,724,250,54,false,()->carousel.smoothScrollBy(-326,0));
        button(page,"Next ›",1150,724,250,54,false,()->carousel.smoothScrollBy(326,0));
        button(page,"Back",112,850,300,76,false,this::back);
        if(campaign)button(page,"Original story character",850,850,550,76,false,()->{settings.characterChoice(true,-1,0);update.run();});
        refreshers.add(update);update.run();
        int selected=settings.characterChoice(campaign);carousel.post(()->carousel.scrollTo(Math.max(0,(selected<0?0:selected&255)*326-326),0));
    }
    private String characterSkinName(int skin){return skin==0?"Default":skin==1?"Jarl":"Skin "+(skin+1);}
    private void showCharacterDetail(boolean campaign,int index,String name,int skins,CharacterCard source,Runnable update) {
        FrameLayout detail=modal(name,"",850,1120);
        detail.setBackground(new Iron(false,true,false,false));
        CharacterCard preview=place(detail,new CharacterCard(index,true),40,112,520,630);
        preview.previewSkin=source.previewSkin;preview.setBackground(new Iron(false,false,false,false));preview.setStateListAnimator(null);preview.setFocusable(false);preview.setClickable(false);preview.setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        label(detail,"Drag to rotate · Flick to spin",40,755,520,44,23,MUTED,false);
        label(detail,"Choose a look",610,130,450,60,34,IVORY,true);
        TextView current=label(detail,"",610,198,450,60,29,GOLD,false);
        label(detail,skins>1?"Preview any skin, then select your character below.":"This character has one original skin.",610,270,450,100,25,MUTED,false);
        ScrollView skinList=place(detail,new ScrollView(getContext()),610,380,450,220);
        LinearLayout skinRows=new LinearLayout(getContext());skinRows.setOrientation(LinearLayout.VERTICAL);skinList.addView(skinRows);
        ArrayList<Button> skinButtons=new ArrayList<>();
        Runnable repaint=()->{current.setText(characterSkinName(preview.previewSkin)+" · "+(preview.previewSkin+1)+" / "+skins);
            for(int i=0;i<skinButtons.size();++i){skinButtons.get(i).setSelected(i==preview.previewSkin);skinButtons.get(i).setText(characterSkinName(i)+(i==preview.previewSkin?"   ✓":""));}};
        for(int i=0;i<skins;++i){final int skin=i;FrameLayout row=new FrameLayout(getContext());skinRows.addView(row,new LinearLayout.LayoutParams(450,76));
            Button option=button(row,characterSkinName(i),0,0,450,66,false,()->{preview.previewSkin=skin;repaint.run();});skinButtons.add(option);}
        repaint.run();
        Button select=button(detail,"Select this character",610,650,450,78,true,()->{settings.characterChoice(campaign,index,preview.previewSkin);back();update.run();source.requestFocusFromTouch();});
        button(detail,"Back to characters",610,748,450,66,false,()->{back();source.requestFocusFromTouch();});
        button(detail,"×",1026,25,54,54,false,()->{back();source.requestFocusFromTouch();});
        select.requestFocusFromTouch();
    }
    final class CharacterCard extends Button {
        final int character,pixelWidth,pixelHeight;
        final boolean expanded;
        int previewSkin=0,lastSkin=-1;
        float yaw=0,lastTouchX,spinVelocity;
        long lastTouchTime,lastPreviewTime;
        int touchPointer=-1;
        boolean unavailable=false;
        int[] pixels;
        Bitmap preview;
        final Paint previewPaint=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
        final Rect visible=new Rect();
        final Runnable animate=new Runnable(){public void run(){
            long now=android.os.SystemClock.uptimeMillis();
            float dt=lastPreviewTime==0?0:Math.min(.1f,(now-lastPreviewTime)/1000f);lastPreviewTime=now;
            if(expanded && touchPointer<0 && Math.abs(spinVelocity)>.02f){yaw=(yaw+spinVelocity*dt)%6.2831853f;spinVelocity*=Math.exp(-2.5f*dt);}
            if((modal==null || expanded) && isShown() && getLocalVisibleRect(visible) && visible.width()>20) {
                if(pixels==null)pixels=new int[pixelWidth*pixelHeight];
                if(lastSkin!=previewSkin){preview=null;lastSkin=previewSkin;unavailable=false;}
                int result=host.characterPreview(character,previewSkin,pixels,expanded?yaw:Float.NaN);
                if(result<0){unavailable=true;invalidate();}
                if(result==1) {
                    if(preview==null)preview=Bitmap.createBitmap(pixelWidth,pixelHeight,Bitmap.Config.ARGB_8888);
                    preview.setPixels(pixels,0,pixelWidth,0,0,pixelWidth,pixelHeight);invalidate();
                }
            }
            if(isAttachedToWindow()){if(expanded)postOnAnimation(this);else postDelayed(this,modal==null&&isShown()&&getLocalVisibleRect(visible)?16:100);}
        }};
        CharacterCard(int character){this(character,false);}
        CharacterCard(int character,boolean expanded){super(RuneMenuView.this.getContext());this.character=character;this.expanded=expanded;pixelWidth=expanded?384:192;pixelHeight=expanded?448:224;}
        @Override public boolean onTouchEvent(MotionEvent event) {
            if(!expanded)return super.onTouchEvent(event);
            // The enlarged image is a drag surface, never a pressed/focused button.
            if(!event.isFromSource(InputDevice.SOURCE_TOUCHSCREEN))return false;

            switch(event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    touchPointer=event.getPointerId(0);lastTouchX=event.getX();lastTouchTime=event.getEventTime();spinVelocity=0;
                    getParent().requestDisallowInterceptTouchEvent(true);return true;
                case MotionEvent.ACTION_MOVE:
                    int pointer=event.findPointerIndex(touchPointer);
                    if(pointer>=0){float x=event.getX(pointer),delta=(x-lastTouchX)*12.566371f/Math.max(1,getWidth());
                        long elapsed=event.getEventTime()-lastTouchTime;
                        yaw=(yaw+delta)%6.2831853f;
                        if(elapsed>0)spinVelocity=Math.max(-18f,Math.min(18f,delta*1000f/elapsed));
                        lastTouchX=x;lastTouchTime=event.getEventTime();host.characterPreviewRotation(character,previewSkin,yaw);}
                    return true;
                case MotionEvent.ACTION_POINTER_UP:
                    if(event.getPointerId(event.getActionIndex())==touchPointer){int next=event.getActionIndex()==0?1:0;touchPointer=event.getPointerId(next);lastTouchX=event.getX(next);lastTouchTime=event.getEventTime();spinVelocity=0;}return true;
                case MotionEvent.ACTION_UP: case MotionEvent.ACTION_CANCEL:
                    if(event.getActionMasked()==MotionEvent.ACTION_CANCEL || event.getEventTime()-lastTouchTime>100)spinVelocity=0;
                    touchPointer=-1;getParent().requestDisallowInterceptTouchEvent(false);return true;
                default:return true;
            }
        }
        protected void onAttachedToWindow(){super.onAttachedToWindow();post(animate);}
        protected void onDetachedFromWindow(){removeCallbacks(animate);preview=null;pixels=null;super.onDetachedFromWindow();}
        protected void onDraw(Canvas canvas){
            super.onDraw(canvas);
            if(preview!=null) {
                float h=Math.min(getHeight()-(expanded?16:52),(getWidth()-16f)*pixelHeight/pixelWidth),w=h*pixelWidth/pixelHeight;
                float top=expanded?(getHeight()-h)/2:8;
                canvas.drawBitmap(preview,null,new RectF((getWidth()-w)/2,top,(getWidth()+w)/2,top+h),previewPaint);
            } else {
                previewPaint.setColor(MUTED);previewPaint.setTextSize(21);previewPaint.setTextAlign(Paint.Align.CENTER);
                canvas.drawText(unavailable?"Preview unavailable":"Preparing preview…",getWidth()/2f,getHeight()/2f,previewPaint);
            }
        }
    }
    static final class CharacterCardBackground extends Drawable {
        final Paint paint=new Paint(3);
        public void draw(Canvas canvas){Rect b=getBounds();boolean active=false,focused=false;
            for(int state:getState()){active|=state==android.R.attr.state_selected;focused|=state==android.R.attr.state_focused;}
            paint.setColor(active?0xff353024:0xff1a2021);paint.setStyle(Paint.Style.FILL);canvas.drawRect(b,paint);
            paint.setColor(active||focused?GOLD:0xff685942);paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(focused?6:3);canvas.drawRect(b.left+4,b.top+4,b.right-4,b.bottom-4,paint);
        }
        public boolean isStateful(){return true;}
        protected boolean onStateChange(int[] state){invalidateSelf();return true;}
        public void setAlpha(int alpha){} public void setColorFilter(ColorFilter filter){} public int getOpacity(){return PixelFormat.TRANSLUCENT;}
    }
    void showFindGames(boolean search){
        screen="findgames";base("");
        label(page,"Find games",112,185,1250,85,58,IVORY,true);
        RuneDiscovery d=host.discovery();
        if(d==null){label(page,"Game discovery could not start.",112,310,1200,100,30,IVORY,false);button(page,"Back",112,825,300,76,false,this::back);return;}
        label(page,"Public games · matching game files required",112,280,1250,65,26,MUTED,false);
        TextView progress=label(page,d.message,112,345,1250,75,27,IVORY,false);
        ScrollView scroll=place(page,new ScrollView(getContext()),112,440,1298,340);
        LinearLayout rows=new LinearLayout(getContext());rows.setOrientation(LinearLayout.VERTICAL);scroll.addView(rows);
        for(int i=0;i<d.rows.length();i++){
            org.json.JSONObject info=d.rows.optJSONObject(i);if(info==null)continue;
            String key=info.optString("key");
            Button row=new Button(getContext());row.setAllCaps(false);row.setTypeface(BODY_FONT);row.setTextColor(IVORY);row.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX,27);
            row.setText(info.optString("name")+" · "+info.optString("mode")+"\n"+info.optString("map")+" · "+info.optInt("players")+" / "+info.optInt("capacity")+(info.optInt("available")==0?" · Full":" · Join"));
            row.setSingleLine(false);row.setMaxLines(2);row.setIncludeFontPadding(false);row.setPadding(24,6,24,6);row.setMinHeight(0);row.setMinimumHeight(0);row.setGravity(Gravity.CENTER_VERTICAL|Gravity.CENTER_HORIZONTAL);row.setStateListAnimator(null);
            row.setOnFocusChangeListener((view,focus)->{android.graphics.drawable.GradientDrawable ring=new android.graphics.drawable.GradientDrawable();ring.setColor(Color.TRANSPARENT);ring.setStroke(2,GOLD);view.setForeground(focus?ring:null);});
            row.setBackground(new Iron(false,false));row.setEnabled(!d.phase.equals("joining")&&info.optInt("available")>0);rows.addView(row,new LinearLayout.LayoutParams(-1,116));
            row.setOnClickListener(v->{Runnable join=()->host.joinDiscovered(key);if(hasSession)confirm("Join this game?","Unsaved campaign progress will be replaced. Your saves are kept.","Join",join);else join.run();});
        }
        button(page,d.busy()?"Cancel":"Back",112,825,300,76,false,this::back);
        Button refresh=button(page,"Refresh",1030,825,380,76,true,()->d.command("browse",null));refresh.setEnabled(!d.busy());
        label(page,"Refresh to look for more games.",112,920,1298,50,22,MUTED,false);
        refresh();if(search)d.command("browse",null);
    }
    private int nextChoice(int current,int[] choices){for(int n:choices)if(n>current)return n;return choices[0];}
    private void hostChoice(String title,String value,int x,int y,Runnable change){
        label(page,title,x,y,610,34,23,MUTED,false);
        Button choice=button(page,value,x,y+40,610,64,false,()->{hostFocus=title;change.run();});
        choice.setContentDescription(title+": "+value);if(title.equals(hostFocus))choice.requestFocus();
    }
    private void showAdvancedJoin(){
        screen="advancedJoin";base("Advanced joining");
        FrameLayout p=panel(84,230,1368,600,"PRIVATE CONNECTIONS");
        button(p,"Open invitation file",40,110,620,78,false,()->host.chooseInvitation());
        button(p,"Join with host identity",40,220,620,78,false,()->{FrameLayout card=modal("Host identity","Paste the 64-character identity shared by the host.",440);EditText input=new EditText(getContext());input.setSingleLine(true);input.setTextColor(IVORY);input.setTextSize(24);place(card,input,42,205,730,70);button(card,"Join",410,335,364,66,true,()->{String key=input.getText().toString().trim();if(!key.matches("[0-9a-fA-F]{64}")){notice="Enter the full 64-character host identity.";refresh();return;}back();host.joinDiscovered(key.toLowerCase(Locale.ROOT));});button(card,"Cancel",42,335,330,66,false,this::back);});
        label(p,"Most players can use Find games.\n\nPrivate invitation files reserve one player slot. Host identities connect through discovery. Keep invitations private.",720,110,580,330,26,MUTED,false);
        footer(false);refresh();
    }
    private void showHostSetup(){
        screen="host";notice="";base("Host a match");hostConfig.normalize(host.hosting().game);
        ArrayList<String> addresses=host.hosting().addresses();if(!addresses.contains(hostAddress))hostAddress=addresses.isEmpty()?"":addresses.get(0);
        button(page,"Match name: "+hostConfig.matchName,112,220,1298,74,false,()->{
            EditText field=new EditText(getContext());field.setSingleLine(true);field.setText(hostConfig.matchName);field.setSelectAllOnFocus(true);field.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(48)});
            new android.app.AlertDialog.Builder(getContext()).setTitle("Match name").setView(field).setNegativeButton("Cancel",null).setPositiveButton("Save",(d,w)->{String name=field.getText().toString().trim();if(!name.matches("[ -~]{1,48}")){setNotice("Use 1–48 simple letters, numbers or symbols.");return;}hostConfig.matchName=name;showHostSetup();}).show();
        });
        hostChoice("Game mode",hostConfig.name(),112,320,()->{hostConfig.mode=RuneHostConfig.MODES[(Arrays.asList(RuneHostConfig.MODES).indexOf(hostConfig.mode)+1)%4];showHostSetup();});
        hostChoice("Visibility",hostConfig.discoverable?"Public · Find games":"Private invitation",800,320,()->{hostConfig.discoverable=!hostConfig.discoverable;showHostSetup();});
        hostChoice("Player capacity · includes you",Integer.toString(hostConfig.players),112,435,()->{hostConfig.players=nextChoice(hostConfig.players,new int[]{2,4,6,8,12,16});showHostSetup();});
        hostChoice("Starting bots",Integer.toString(hostConfig.bots),800,435,()->{hostConfig.bots=(hostConfig.bots+1)%hostConfig.players;showHostSetup();});
        hostChoice("Laws of Battle","Players and bots",112,665,()->{showBattleLaws("host");});
        hostChoice("Map rotation",hostConfig.maps.isEmpty()?"Choose maps":hostConfig.maps.get(0)+(hostConfig.maps.size()>1?" + "+(hostConfig.maps.size()-1)+" more":""),112,550,this::showHostMaps);
        hostChoice("Match rules",(hostConfig.score==0?"No score limit":hostConfig.score+" points")+" · "+(hostConfig.minutes==0?"No time limit":hostConfig.minutes+" min"),800,550,this::showHostRules);
        label(page,"Player capacity includes the host, other players and bots.\nThe host must remain open while others play.",112,780,1298,50,20,MUTED,false);
        button(page,"Back",112,840,300,76,false,this::back);
        Button start=button(page,"Start & join",1030,840,380,76,true,()->confirm("Start this match?",(hostConfig.discoverable?"Your match will appear in Find games. ":"Share a separate invitation with each player. ")+"Unsaved campaign progress will be replaced; saved games are kept.","Host",()->{try{host.hosting().start(hostConfig,hostAddress);showMultiplayer();}catch(RuntimeException e){setNotice(e.getMessage());}}));
        refreshers.add(()->start.setEnabled(host.ready()&&(hostConfig.discoverable||!hostAddress.isEmpty())&&!hostConfig.maps.isEmpty()));status=label(page,"",445,840,550,76,23,MUTED,false);refresh();
    }

    private void showBattleLaws(String scope){
        boolean multi=scope.equals("host")||scope.equals("match"),readOnly=scope.equals("match");
        screen=scope.equals("host")?"hostLaws":scope.equals("match")?"matchLaws":scope.equals("new")?"newLaws":"campaignLaws";
        base("Laws of Battle / "+(multi?"Multiplayer":"Single player"));
        int mask=readOnly?MainActivity.nativeBattleLaws():multi?hostConfig.laws:RuneBattleLaws.campaign(settings);
        FrameLayout body=panel(84,200,1368,674,readOnly?"CURRENT HOST RULES":multi?"NEW HOSTED MATCH · PLAYERS AND BOTS":"CAMPAIGN · PLAYERS AND ELIGIBLE ENEMIES");
        ScrollView scroll=place(body,new ScrollView(getContext()),24,100,1320,550);
        scroll.setFillViewport(false);scroll.setClipToPadding(false);scroll.setPadding(0,0,14,8);
        scroll.setVerticalScrollBarEnabled(true);scroll.setScrollbarFadingEnabled(false);
        scroll.setContentDescription("Laws of Battle rules");
        LinearLayout list=new LinearLayout(getContext());list.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(list,new ScrollView.LayoutParams(-1,-2));
        for(int row=0;row<RuneBattleLaws.NAMES.length;row++){
            final int index=row,bit=1<<index;
            boolean locked=readOnly||(!multi&&bit==RuneBattleLaws.LEAP)||(multi&&bit==1024)||(bit==RuneBattleLaws.CROSS&&(mask&RuneBattleLaws.DUAL)==0);
            boolean checked=(RuneBattleLaws.effective(mask)&bit)!=0;
            FrameLayout card=new FrameLayout(getContext());card.setBackground(new Iron(false,false,checked,false));
            LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,166);lp.bottomMargin=12;list.addView(card,lp);
            place(card,new BattleLawIcon(getContext(),index,locked),20,36,78,78);
            label(card,RuneBattleLaws.NAMES[index],116,15,1010,40,28,locked?MUTED:IVORY,false);
            String constraint=!multi&&bit==RuneBattleLaws.LEAP?"Disabled in campaign":multi&&bit==1024?"Campaign only":bit==RuneBattleLaws.CROSS&&(mask&RuneBattleLaws.DUAL)==0?"Requires dual wield":readOnly?"Host rule":"";
            TextView help=label(card,(constraint.isEmpty()?"":constraint+" · ")+RuneBattleLaws.HELP[index],116,63,1050,90,23,MUTED,false);
            help.setGravity(Gravity.TOP);help.setLineSpacing(2,1);
            ToggleButton switchView=place(card,new ToggleButton(getContext()),1190,53,80,54);
            switchView.setTextOn("");switchView.setTextOff("");switchView.setChecked(checked);switchView.setBackground(new ToggleArt());switchView.setPadding(0,0,0,0);switchView.setMinWidth(0);switchView.setMinimumWidth(0);switchView.setMinHeight(0);switchView.setMinimumHeight(0);switchView.setStateListAnimator(null);switchView.setClickable(false);switchView.setFocusable(false);switchView.setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);switchView.setAlpha(locked?.45f:1);
            Button toggle=button(card,"",0,0,1288,166,false,()->{
                if(multi){hostConfig.laws^=bit;hostConfig.normalBloodlust=(hostConfig.laws&RuneBattleLaws.NORMAL)!=0;settings.prefs.edit().putInt("laws.host",hostConfig.laws).apply();}
                else RuneBattleLaws.campaign(settings,RuneBattleLaws.campaign(settings)^bit);
                showBattleLaws(scope);
            });
            toggle.setTag("law:"+index);toggle.setBackgroundColor(Color.TRANSPARENT);toggle.setContentDescription(RuneBattleLaws.NAMES[index]+", "+(checked?"On":"Off")+(constraint.isEmpty()?"":", "+constraint));
            toggle.setEnabled(!locked);
        }
        FrameLayout f=place(page,new FrameLayout(getContext()),0,906,1536,118);f.setBackgroundColor(0xee0b1011);
        button(f,"Back",50,24,210,58,false,this::back);
        label(f,readOnly?"Rules are set by the host":"Applies to players and eligible enemies",300,24,600,58,22,MUTED,false);
        if(scope.equals("new"))button(f,"Begin campaign",1000,24,470,58,true,()->confirm("Start a new campaign?","Use these Laws of Battle. Unsaved progress will be replaced; existing save slots are kept.","Begin",()->runSession(1,9)));
        else if(!readOnly)button(f,"Physics: "+RuneSettings.PHYSICS[multi?hostConfig.physics:settings.get("physics")],930,24,540,58,false,()->{
            if(multi){hostConfig.physics=(hostConfig.physics+1)%3;settings.prefs.edit().putInt("laws.hostPhysics",hostConfig.physics).apply();}else settings.set("physics",(settings.get("physics")+1)%3);
            showBattleLaws(scope);
        });
    }

    private void showHostRules(){
        screen="hostRules";base("Match rules");
        int y=240;
        if(!hostConfig.mode.equals("dm")){hostChoice(hostConfig.mode.equals("arena")?"Arena team size":"Teams",hostConfig.mode.equals("arena")?hostConfig.arenaSize+" vs "+hostConfig.arenaSize:Integer.toString(hostConfig.teams),112,y,()->{if(hostConfig.mode.equals("arena"))hostConfig.arenaSize=hostConfig.arenaSize%4+1;else hostConfig.teams=hostConfig.teams==4?2:hostConfig.teams+1;hostConfig.normalize(host.hosting().game);showHostRules();});}
        hostChoice(hostConfig.mode.equals("arena")?"Round limit":"Score limit",hostConfig.score==0?"No limit":Integer.toString(hostConfig.score),800,y,()->{hostConfig.score=nextChoice(hostConfig.score,new int[]{0,5,10,20,30,50,100});if(hostConfig.score==0&&hostConfig.minutes==0)hostConfig.minutes=10;showHostRules();});
        hostChoice("Time limit",hostConfig.minutes==0?"No limit":hostConfig.minutes+" minutes",112,360,()->{hostConfig.minutes=nextChoice(hostConfig.minutes,new int[]{0,5,10,15,20,30,60});if(hostConfig.minutes==0&&hostConfig.score==0)hostConfig.score=20;showHostRules();});
        hostChoice("Laws of Battle","Choose permitted moves",800,360,()->{showBattleLaws("host");});
        hostChoice("Physics mode",RuneSettings.PHYSICS[hostConfig.physics],112,580,()->{hostConfig.physics=(hostConfig.physics+1)%3;settings.prefs.edit().putInt("laws.hostPhysics",hostConfig.physics).apply();showHostRules();});
        if(hostConfig.mode.equals("headball"))hostChoice("Severed limbs score",hostConfig.limbs?"On":"Off",112,480,()->{hostConfig.limbs=!hostConfig.limbs;showHostRules();});
        if(!hostConfig.discoverable){ArrayList<String> addresses=host.hosting().addresses();hostChoice("Local network address",hostAddress.isEmpty()?"Connect to Wi-Fi":hostAddress,800,480,()->{if(!addresses.isEmpty())hostAddress=addresses.get((addresses.indexOf(hostAddress)+1)%addresses.size());showHostRules();});}
        label(page,"These rules apply to the new hosted match. Campaign preferences are unchanged.\nKicks, shield bash, dual wield and bloodlust moves follow the host’s supported combat rules.",112,650,1298,130,25,MUTED,false);footer(false);refresh();
    }
    private void showHostMaps(){
        screen="maps";notice="";base("Map rotation");
        label(page,hostConfig.name()+" · selected order · up to 32 maps",84,198,1350,55,26,MUTED,false);
        FrameLayout chosen=panel(44,270,712,570,"SELECTED ROTATION");FrameLayout available=panel(780,270,712,570,"AVAILABLE MAPS");
        ScrollView order=place(chosen,new ScrollView(getContext()),26,95,660,450);LinearLayout selected=new LinearLayout(getContext());selected.setOrientation(LinearLayout.VERTICAL);order.addView(selected);
        for(int i=0;i<hostConfig.maps.size();i++){final int index=i;String map=hostConfig.maps.get(i);FrameLayout row=new FrameLayout(getContext());selected.addView(row,new LinearLayout.LayoutParams(660,82));label(row,(i+1)+". "+map,0,0,370,74,25,IVORY,false);
            Button up=button(row,"↑",375,6,72,64,false,()->{Collections.swap(hostConfig.maps,index,index-1);showHostMaps();});up.setEnabled(i>0);up.setContentDescription("Move "+map+" earlier");
            Button down=button(row,"↓",455,6,72,64,false,()->{Collections.swap(hostConfig.maps,index,index+1);showHostMaps();});down.setEnabled(i+1<hostConfig.maps.size());down.setContentDescription("Move "+map+" later");
            button(row,"Remove",535,6,120,64,false,()->{hostConfig.maps.remove(index);showHostMaps();});}
        ScrollView catalog=place(available,new ScrollView(getContext()),26,95,660,450);LinearLayout list=new LinearLayout(getContext());list.setOrientation(LinearLayout.VERTICAL);catalog.addView(list);
        for(String map:hostConfig.available(host.hosting().game)){FrameLayout row=new FrameLayout(getContext());list.addView(row,new LinearLayout.LayoutParams(660,82));Button add=button(row,(hostConfig.maps.contains(map)?"✓  ":"+  ")+map,0,4,660,72,false,()->{if(hostConfig.maps.size()<32&&!hostConfig.maps.contains(map)){hostConfig.maps.add(map);showHostMaps();}});add.setEnabled(!hostConfig.maps.contains(map)&&hostConfig.maps.size()<32);}
        button(page,"Back",112,865,300,68,false,()->{if(hostConfig.maps.isEmpty()){setNotice("Choose at least one map.");return;}back();});label(page,hostConfig.maps.size()+(hostConfig.maps.size()==1?" map selected":" maps selected"),800,865,610,68,25,GOLD,false);
        status=label(page,"",112,950,1290,50,22,MUTED,false);refresh();
    }
    void showInvitations(){
        RuneHosting hosting=host.hosting();if(!hosting.ready()){showMultiplayer();return;}
        screen="invitations";notice="";base("");invitationSlot=Math.max(2,Math.min(invitationSlot,hosting.players));
        label(page,"Invite players",112,185,1250,85,58,IVORY,true);
        if(hosting.discoverable){
            label(page,"Public match · friends choose Multiplayer → Find games",112,300,1250,100,30,IVORY,false);
            TextView progress=label(page,hosting.discoveryStatus,112,420,1250,140,28,MUTED,false);
            refreshers.add(()->progress.setText(hosting.discoveryStatus));
            label(page,"Player slots are assigned automatically. Keep this host open.\nSome networks may block peer connections; private invitations remain available for other matches.",112,610,1250,130,26,MUTED,false);
            button(page,"Back",112,825,300,76,false,this::back);refresh();return;
        }
        label(page,"Hosting on "+hosting.address+" · your device uses player slot 1",112,290,1250,70,28,IVORY,false);
        label(page,"Send a different invitation to each player. They choose Multiplayer → Join with invitation.\nKeep invitations private; anyone holding one can use that slot.\nThe host must stay open. For remote play, use a shared VPN or reachable direct connection.",112,380,1250,160,26,MUTED,false);
        hostChoice("Invitation slot","Player "+invitationSlot,112,565,()->{invitationSlot=invitationSlot>=hosting.players?2:invitationSlot+1;showInvitations();});
        button(page,"Share invitation",800,605,610,64,true,()->{try{hosting.share(invitationSlot);}catch(Exception e){setNotice("Could not share: "+e.getMessage());}});
        button(page,"Save invitation file",800,705,610,64,false,()->host.saveInvitation(invitationSlot));
        button(page,"Back",112,825,300,76,false,this::back);
        status=label(page,"",455,820,955,95,24,MUTED,false);refresh();
    }
    boolean hasSession=true;
    private void showDeath() {
        if(host.multiplayer()){showMultiplayer();return;}
        screen="death";base("");
        label(page,"You have fallen",112,190,900,92,64,IVORY,true);
        boolean practice=host.practiceArena();
        TextView recovery=label(page,"",112,282,1150,65,28,MUTED,false);
        Button checkpoint=button(page,practice?"Restart fight":"Retry checkpoint",112,388,590,86,true,
            ()->runSession(host.practiceArena()?8:4,10));
        Button load=button(page,"Load game",112,492,590,86,false,()->showSaves(false));
        button(page,"Return to title",112,596,590,86,false,()->{titleMode=true;showHome();});
        refreshers.add(()->{
            File file=saveFile(10);
            boolean available=file!=null&&file.isFile();
            checkpoint.setEnabled(host.ready()&&(practice||available));
            recovery.setText(practice?"Restart this practice fight. Your campaign saves are kept.":
                available?"Return to a checkpoint or load a saved game.":"No checkpoint is available. Load a saved game or return to the title.");
            load.setEnabled(host.ready());
        });
        status=label(page,notice,112,726,1150,110,25,MUTED,false);
        refresh();
    }
    private String chapter() {
        if(!hasSession)return "Open Rune";
        String[] fields=host.metadata().split("\n");
        if(fields.length==0 || fields[0].isEmpty())return "Open Rune";
        return fields[0].replaceAll("(?i)\\.(run|rsa)$","").replaceAll("([a-z])([A-Z])","$1 $2");
    }
    void showSettings(String tab){
        if(!Arrays.asList(TABS).contains(tab)&&!tab.equals("Support")&&!tab.equals("Connection"))tab="Video";
        lastSettings=tab;screen="settings:"+tab;settingsFrame(tab,tab);
        FrameLayout p=panel(370,216,1122,650,tab.equals("Support")?"HELP & SUPPORT":tab.toUpperCase(Locale.ROOT));
        String[] sections=RuneMenuSections.sections(tab);
        for(int i=0;i<sections.length;i++){final String key=sections[i];button(p,RuneMenuSections.title(key),36,100+i*90,1050,72,false,()->showSection(lastSettings,key));}
        if(tab.equals("Video")&&settings.comparing)button(p,"Restore my visuals",36,560,1050,60,false,()->{settings.compare();showSettings("Video");});
        footer(false);refresh();p.post(()->{if(rememberedFocus.getOrDefault(screen,"").isEmpty())focusFirstControl(p);});
    }
    private void settingsFrame(String category,String heading){
        base(heading);int y=210;
        for(String tab:TABS){button(page,tab,44,y,292,62,tab.equals(category),()->selectCategory(tab));y+=76;}
        button(page,"About & support",44,690,292,62,category.equals("Support"),()->selectCategory("Support"));
        if(category.equals("Connection"))button(page,"Connection",44,780,292,62,true,()->showSettings("Connection"));
    }
    private void showSection(String category,String section){
        if(section.equals("combat")){showBattleLaws(host.multiplayer()?"match":"campaign");return;}
        lastSettings=category;screen="settings:"+category+":"+section;settingsFrame(category,category+" / "+RuneMenuSections.title(section));
        FrameLayout p=panel(370,216,750,650,RuneMenuSections.title(section).toUpperCase(Locale.ROOT));
        FrameLayout help=panel(1140,216,352,650,"DETAILS");
        settingHelp=label(help,RuneMenuSections.help(section),26,100,300,470,23,IVORY,false);
        String[] keys=RuneMenuSections.keys(section);int y=92,left=36,width=678;
        FrameLayout controls=p;
        if(section.equals("effects")) {
            // Seven effect rows exceed the space above Compare. Keep their
            // drawing, hit targets and controller focus inside a scroll viewport.
            ScrollView scroll=place(p,new ScrollView(getContext()),36,92,678,442);
            scroll.setFillViewport(false);scroll.setClipChildren(true);scroll.setClipToPadding(true);
            scroll.setVerticalScrollBarEnabled(true);scroll.setScrollbarFadingEnabled(false);
            scroll.setContentDescription("Effects and weather controls");
            scroll.setDescendantFocusability(ViewGroup.FOCUS_AFTER_DESCENDANTS);
            controls=new FrameLayout(getContext());
            scroll.addView(controls,new ScrollView.LayoutParams(-1,-2));
            left=0;y=0;width=664;
        }
        for(String key:keys){if(key.endsWith("Manual"))continue;if(settings.items.get(key).choices==null){slider(controls,key,left,y,width);y+=102;}else{row(controls,key,left,y,width,74);y+=74;}}
        if(section.equals("display")){TextView actual=label(p,host.scalingStatus()+"\n"+host.timingStatus(),36,550,678,85,19,MUTED,false);refreshers.add(()->actual.setText(host.scalingStatus()+"\n"+host.timingStatus()));}
        if(section.equals("camera")){TextView actual=label(p,host.cameraStatus(),36,400,678,150,24,GOLD,false);refreshers.add(()->actual.setText(host.cameraStatus()));}
        if(section.equals("textures")){
            textureInstallLabel=label(p,"",36,482,678,132,21,MUTED,false);
            refreshers.add(this::updateTextureInstallStatus);
            updateTextureInstallStatus();
        }
        if(section.equals("effects")){Button compare=button(p,"",36,550,678,64,false,()->{settings.compare();refresh();});refreshers.add(()->compare.setText(settings.comparing?"Restore my visuals":"Compare original visuals"));}
        if(section.equals("touch"))button(p,"Edit touch controls",36,300,678,72,false,()->host.editTouch());
        if(section.equals("hud"))button(p,"Edit HUD layout",36,Math.max(300,y+16),678,72,false,()->host.editHud());
        if(section.equals("campaign"))button(p,"Campaign character & skin",36,250,678,72,false,()->showCharacters(true));
        if(section.equals("gamepad")){label(p,host.controllerStatus(),36,110,678,100,25,GOLD,false);label(p,"Move / look: left / right stick\nJump: A · Crouch / dive: B\nAttack: X / RT · Block: LB / LT\nUse: Y / left-stick click · Throw: RB\nKick: right-stick click · Rune Power: Select / View\nWeapons: D-pad · Pause: Start\nShield bash: double-tap Block\nTwo-hand slam: 2× Jump, then Attack\nBloodlust: double-tap Jump or Block + Attack",36,240,678,350,24,IVORY,false);}
        if(section.equals("input"))label(p,"Keyboard: WASD move · Enter attack · Space jump\nRight Ctrl block · Left Ctrl use · E Rune Power\nF kick · Q throw · Escape menu",36,360,678,200,23,MUTED,false);
        if(section.equals("vibration")){button(p,"Test vibration",36,470,678,68,false,host::testRumble);TextView actual=label(p,host.rumbleStatus(),36,550,678,75,21,MUTED,false);refreshers.add(()->actual.setText(host.rumbleStatus()));}
        if(section.equals("updates")){
            label(p,RuneUpdates.identity(),36,92,678,80,23,GOLD,false);
            TextView releaseStatus=label(p,RuneUpdates.current.message,36,180,678,90,24,IVORY,false);
            Button check=button(p,"Check for updates",36,280,328,64,false,()->RuneUpdates.check(getContext(),true));
            Button auto=button(p,"",386,280,328,64,false,()->RuneUpdates.setAutomatic(getContext(),!RuneUpdates.automatic(getContext())));
            Button downloads=button(p,"Open releases page",36,365,678,64,false,()->{if(!RuneUpdates.open(getContext()))setNotice("Could not open your browser.");});
            TextView notes=label(p,"",36,450,678,170,22,MUTED,false);notes.setMovementMethod(new android.text.method.ScrollingMovementMethod());notes.setVerticalScrollBarEnabled(true);
            refreshers.add(()->{RuneUpdates.Result r=RuneUpdates.current;releaseStatus.setText(r.message);check.setEnabled(!r.busy);auto.setText(RuneUpdates.automatic(getContext())?"Startup checks: On":"Startup checks: Off");downloads.setText(r.available?"View update & downloads":"Open releases page");notes.setText(r.notes.isEmpty()?"The client and host must use compatible network and gameplay rules. Your saves, settings and game files stay separate from updates.":r.notes);});
        }
        if(section.equals("support")){button(p,"Licenses & notices",36,170,678,72,false,()->showNotices("licenses"));button(p,"Developer tools",36,270,678,72,false,this::showDeveloper);label(p,"Open Rune · SurrealEngine fork\nNative ARM64 · Vulkan\nRequires your original Rune game files.",36,390,678,170,24,MUTED,false);}
        footer(true);refresh();p.post(()->{if(rememberedFocus.getOrDefault(screen,"").isEmpty())focusFirstControl(p);});
        if(section.equals("campaign"))status.setText("Applies to a new campaign; saves retain their difficulty.");
        if(section.equals("combat")||section.equals("enemies"))status.setText(host.multiplayer()?"Host-controlled during multiplayer":"Campaign preference · multiplayer uses host rules");
    }
    private boolean hostControlled(String key){return host.multiplayer()&&Arrays.asList("difficulty","tactics","kick","bash","dualWield","bloodlustMoves","normalBloodlust","physics").contains(key);}
    private String dependency(String key){if(key.equals("soundRemaster")&&!new File(getContext().getFilesDir(),"game/SoundPacks/OpenRune-Layered-v1/runtime.json").isFile())return "Optional sound pack is not installed. Original sounds will be used.";if((key.equals("materialMaps")||key.equals("shadowLights")||key.equals("materialLighting")||key.equals("environmentProbes"))&&settings.get("pixelLighting")!=2)return "Requires Remastered lighting. Your saved choice is kept.";if(key.equals("scalingMethod")&&settings.get("resolution")==0)return "Native resolution bypasses upscaling. Your saved method is kept.";return "";}
    private void updateHelp(String key){if(settingHelp==null)return;RuneSettings.Item i=settings.items.get(key);String note=hostControlled(key)?"Host-controlled in this match.":dependency(key);settingHelp.setText(i.name+"\n\n"+i.hint+(note.isEmpty()?"":"\n\n"+note));}
    private void customRate(String key){
        boolean refreshRate=key.equals("refresh");
        FrameLayout card=modal(refreshRate?"Custom refresh rate":"Custom FPS target",refreshRate?"Enter a supported display rate from 30 to 1000 Hz.":"Enter a game target from 30 to 1000 FPS.",500);
        EditText input=new EditText(getContext());input.setSingleLine(true);input.setTextColor(IVORY);input.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX,28);input.setBackgroundTintList(ColorStateList.valueOf(GOLD));input.setHighlightColor(0x66edbd68);input.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);
        input.setImeOptions(android.view.inputmethod.EditorInfo.IME_ACTION_DONE|android.view.inputmethod.EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        input.setContentDescription(refreshRate?"Custom refresh rate value":"Custom FPS target value");
        input.setText(String.valueOf(settings.get(key+"Manual")));input.setSelectAllOnFocus(true);place(card,input,42,210,730,70);
        TextView error=label(card,"",42,288,730,75,22,0xffffb890,false);
        Runnable hide=()->{android.view.inputmethod.InputMethodManager ime=(android.view.inputmethod.InputMethodManager)getContext().getSystemService(Context.INPUT_METHOD_SERVICE);if(ime!=null)ime.hideSoftInputFromWindow(input.getWindowToken(),0);};
        button(card,"Cancel",42,400,300,62,false,()->{hide.run();back();});
        Runnable apply=()->{
            hide.run();
            int value;try{value=Integer.parseInt(input.getText().toString().trim());}catch(NumberFormatException e){error.setText("Enter a whole number from 30 to 1000.");return;}
            if(value<30||value>1000){error.setText("Enter a whole number from 30 to 1000.");return;}
            if(refreshRate&&!host.refreshSupported(value)){error.setText("This display does not report support for "+value+" Hz.");return;}
            settings.customRate(key,value);back();refresh();
        };
        button(card,"Apply",430,400,342,62,true,apply);
        input.setOnEditorActionListener((v,action,event)->{
            if(action==android.view.inputmethod.EditorInfo.IME_ACTION_DONE||(event!=null&&event.getKeyCode()==KeyEvent.KEYCODE_ENTER&&event.getAction()==KeyEvent.ACTION_DOWN)){apply.run();return true;}return false;
        });
    }
    private void rows(FrameLayout panel,String[] keys,int top,int height,int width) {
        for(int i=0;i<keys.length;i++)row(panel,keys[i],36,top+i*height,width,height);
    }
    private void row(FrameLayout panel,String key,int x,int y,int w,int h) {
        RuneSettings.Item item=settings.items.get(key);
        if(host.multiplayer()&&(key.equals("kick")||key.equals("bash")||key.equals("tactics")||key.equals("dualWield")||key.equals("bloodlustMoves")||key.equals("normalBloodlust")||key.equals("difficulty"))) {
            label(panel,item.name,x+6,y,w-240,h,24,IVORY,false);
            label(panel,"Host rules",x+w-230,y,230,h,23,GOLD,false);return;
        }
        String required=dependency(key);
        if(!required.isEmpty()){TextView name=label(panel,item.name,x+6,y,w-250,h,24,MUTED,false);Button why=button(panel,"Unavailable · why?",x+w-250,y+8,250,h-16,false,()->{updateHelp(key);notice=required;refresh();});why.setTag(key);return;}
        int valueWidth=w>700?270:260;
        TextView name=label(panel,item.name+(key.equals("ambientOcclusion")?" · Experimental":""),x+6,y,w-valueWidth-18,h,24,IVORY,false);
        name.setMaxLines(2);name.setContentDescription(item.name+". "+item.hint);
        name.setOnClickListener(v->{updateHelp(key);choices(key);});
        if(item.choices==RuneSettings.SWITCH) {
            ToggleButton toggle=new ToggleButton(getContext());
            toggle.setAllCaps(false);toggle.setTextOn("On");toggle.setTextOff("Off");toggle.setChecked(item.value!=0);
            toggle.setTextColor(IVORY);toggle.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX,24);
            toggle.setPadding(76,0,0,0);toggle.setBackground(new ToggleArt());toggle.setStateListAnimator(null);
            place(panel,toggle,x+w-170,y+8,170,h-16);
            toggle.setTag(key);toggle.setOnFocusChangeListener((v,focused)->{if(focused)updateHelp(key);});
            toggle.setOnCheckedChangeListener((v,on)-> {updateHelp(key); if(item.value!=(on?1:0)){settings.set(key,on?1:0);refresh();} });
            refreshers.add(()->{toggle.setChecked(item.value!=0);toggle.setContentDescription(item.name+": "+item.label());});
            View rule=new View(getContext());rule.setBackgroundColor(0x554e504c);place(panel,rule,x,y+h-1,w,1);return;
        }
        int inset=h<=44?3:9, valueHeight=h-2*inset;
        FrameLayout value=place(panel,new FrameLayout(getContext()),x+w-valueWidth,y+inset,valueWidth,valueHeight);
        value.setBackground(new Iron(false,false));
        Button left=button(value,"‹",0,0,50,valueHeight,false,()->cycle(key,-1));
        Button right=button(value,"›",valueWidth-50,0,50,valueHeight,false,()->cycle(key,1));
        Button select=button(value,"",50,0,valueWidth-100,valueHeight,false,()->choices(key));
        left.setTag(key);right.setTag(key);select.setTag(key);
        left.setBackgroundColor(Color.TRANSPARENT);right.setBackgroundColor(Color.TRANSPARENT);select.setBackgroundColor(Color.TRANSPARENT);
        left.setContentDescription("Decrease "+item.name);right.setContentDescription("Increase "+item.name);
        select.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX,22);
        if(key.equals("texturePack")){select.setSingleLine(true);select.setPadding(4,0,4,0);}
        refreshers.add(()-> { select.setText(settings.label(key));select.setContentDescription(item.name+": "+settings.label(key)); });
        View rule=new View(getContext());rule.setBackgroundColor(0x554e504c);place(panel,rule,x,y+h-1,w,1);
    }
    private void cycle(String key,int delta) {
        updateHelp(key);if(hostControlled(key))return;
        RuneSettings.Item item=settings.items.get(key);
        int choice=(item.value+delta+item.choices.length)%item.choices.length;
        if(key.equals("refresh"))while(choice<item.choices.length-1&&!host.refreshSupported(new int[]{0,60,90,120,144,165}[choice]))choice=(choice+delta+item.choices.length)%item.choices.length;
        if((key.equals("fps")||key.equals("refresh"))&&choice==item.choices.length-1){customRate(key);return;}
        if(key.equals("texturePack"))host.selectTextureArtwork(choice);
        else settings.set(key,choice);
        if(key.equals("resolution")||key.equals("pixelLighting"))restorePage(screen);else refresh();
    }
    private void slider(FrameLayout panel,String key,int x,int y,int w) {
        RuneSettings.Item item=settings.items.get(key);
        label(panel,item.name,x,y,w-110,38,25,IVORY,false);
        TextView percent=label(panel,item.label(),x+w-100,y,100,38,24,GOLD,false);percent.setGravity(Gravity.RIGHT|Gravity.CENTER_VERTICAL);
        int min=key.equals("fov")?70:0,max=key.equals("fov")?120:100;
        SeekBar seek=new SeekBar(getContext());seek.setMax(max-min);seek.setProgress(item.value-min);
        seek.setProgressTintList(ColorStateList.valueOf(GOLD));seek.setThumbTintList(ColorStateList.valueOf(GOLD));seek.setProgressBackgroundTintList(ColorStateList.valueOf(0xff454642));
        seek.setContentDescription(item.name);place(panel,seek,x-8,y+38,w+16,52);
        seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar s,int p,boolean fromUser) { if(fromUser){settings.set(key,p+min);percent.setText(item.label());} }
            public void onStartTrackingTouch(SeekBar s){} public void onStopTrackingTouch(SeekBar s){}
        });
    }
    private FrameLayout modal(String title,String explanation,int height) {return modal(title,explanation,height,816);}
    private FrameLayout modal(String title,String explanation,int height,int width) {
        if(modal!=null)back();
        hiddenAccessibility.clear();
        for(int i=0;i<page.getChildCount();i++) {
            View child=page.getChildAt(i);hiddenAccessibility.put(child,child.getImportantForAccessibility());
            child.setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS);
        }
        modal=place(page,new FrameLayout(getContext()),0,0,1536,1024);modal.setBackgroundColor(0xc9000000);modal.setClickable(true);
        modal.setFocusableInTouchMode(true);modal.requestFocus();
        FrameLayout card=place(modal,new FrameLayout(getContext()),(1536-width)/2,(1024-height)/2,width,height);card.setBackground(new Iron(false,true));
        label(card,title,42,25,width-145,66,36,IVORY,true);
        label(card,explanation,42,105,730,100,24,MUTED,false);return card;
    }
    private void choices(String key) {
        if(hostControlled(key)){notice="This setting is controlled by the host.";refresh();return;}
        RuneSettings.Item item=settings.items.get(key);
        FrameLayout card=modal(item.name,item.hint,260+item.choices.length*68);
        for(int i=0;i<item.choices.length;i++) { final int choice=i;
            boolean available=!key.equals("refresh")||i==item.choices.length-1||host.refreshSupported(new int[]{0,60,90,120,144,165}[i]);
            Button option=button(card,item.choices[i]+(!available?" · Not supported":"")+(i==item.value?"   ✓":""),42,212+i*68,730,60,i==item.value,()->{
                if((key.equals("fps")||key.equals("refresh"))&&choice==item.choices.length-1){customRate(key);return;}
                back();
                if(key.equals("texturePack"))host.selectTextureArtwork(choice);
                else settings.set(key,choice);
                if(key.equals("resolution")||key.equals("pixelLighting"))restorePage(screen);else refresh();
            });
            option.setEnabled(available);if(!available)option.setTextColor(MUTED);
        }
        button(card,"×",738,24,52,52,false,()->back());
    }
    void confirm(String title,String explanation,String action,Runnable next) {
        FrameLayout card=modal(title,explanation,360);
        button(card,"Cancel",42,245,330,66,false,()->back());
        button(card,action,410,245,364,66,true,()->{back();next.run();});
    }
    private void runSession(int action,int slot) {
        if(host.session(action,slot)) {notice="Preparing your saga…";host.resume();}
        else {notice="The world is still loading. Try again in a moment.";refresh();}
    }
    private File saveFile(int slot) {
        String[] data=host.metadata().split("\n");
        if(data.length<3)return null;return new File(data[1],"Save"+slot+"."+data[2]);
    }
    void showSaves(boolean saving) {
        screen=saving?"save":"load";base(saving?"Save game":"Load game");
        label(page,saving?"Choose a slot. Existing saves ask before being replaced.":"Your quick save, checkpoint and manual saves.",86,194,1320,60,25,MUTED,false);
        FrameLayout p=panel(44,276,1448,580,saving?"SAVE YOUR SAGA":"RETURN TO YOUR SAGA");
        ScrollView scroll=place(p,new ScrollView(getContext()),30,94,1388,466);scroll.setFillViewport(false);
        LinearLayout list=new LinearLayout(getContext());list.setOrientation(LinearLayout.VERTICAL);scroll.addView(list);
        int[] slots=saving?new int[]{9,0,1,2,3,4,5,6,7,8}:new int[]{9,10,0,1,2,3,4,5,6,7,8};
        for(int slot:slots) {
            File file=saveFile(slot);boolean exists=file!=null&&file.isFile();
            String name=slot==9?"Quick save":slot==10?"Checkpoint":"Manual save "+(slot+1);
            String description=exists?new SimpleDateFormat("MMM d, yyyy · h:mm a",Locale.US).format(new Date(file.lastModified())):"Empty slot";
            if(exists) {try { File map=new File(file.getParent(),"Save"+slot+".mapname");
                if(map.isFile())description=new String(java.nio.file.Files.readAllBytes(map.toPath()),StandardCharsets.UTF_8).trim()+"  ·  "+description;
            }catch(IOException ignored){} }
            FrameLayout row=new FrameLayout(getContext());LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,92);lp.bottomMargin=12;list.addView(row,lp);
            row.setBackground(new Iron(false,false));
            label(row,name,25,10,490,38,29,IVORY,true);label(row,description,25,53,1030,30,21,MUTED,false);
            Button act=button(row,saving?"Save":"Load",1110,14,235,64,saving||exists,()-> {
                Runnable action=()->runSession(saving?(slot==9?2:6):7,slot);
                if(saving&&!exists)action.run();
                else confirm(saving?"Replace this save?":"Load this save?",saving?"The current contents of "+name.toLowerCase(Locale.US)+" will be replaced.":"Your unsaved progress will be replaced.",saving?"Save":"Load",action);
            });
            act.setEnabled(host.ready()&&(saving||exists));act.setContentDescription((saving?"Save to ":"Load ")+name);
        }
        footer(false);refresh();
    }
    private void showDeveloper() {
        screen="developer";base("Developer tools");
        FrameLayout p=panel(44,276,1448,580,"VALIDATION");
        label(p,"These tools are for testing the port. Starting the combat arena replaces the current unsaved session; campaign saves remain available.",42,104,1300,125,29,MUTED,false);
        button(p,"New combat arena",42,290,620,80,false,()->confirm("Start a test fight?","Save any campaign progress first.","Start fight",()->runSession(5,9)));
        footer(false);refresh();
    }
    private void showAbout() {showSettings("Support");}
    private void showNotices(String path) {
        screen="notices:"+path;base("Licenses & notices");
        ScrollView scroll=place(page,new ScrollView(getContext()),70,210,1396,650);
        LinearLayout list=new LinearLayout(getContext());list.setOrientation(LinearLayout.VERTICAL);scroll.addView(list);
        try {
            for(String name:getContext().getAssets().list(path)) {
                Button item=new Button(getContext());item.setText(name);item.setAllCaps(false);item.setTextColor(IVORY);
                // The menu is already scaled as a whole; density-scaled text and
                // default button insets would overflow these design-space rows.
                item.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX,25);item.setPadding(24,16,24,16);
                item.setIncludeFontPadding(false);item.setMinHeight(76);item.setMinimumHeight(76);
                item.setSingleLine(false);item.setGravity(Gravity.CENTER);item.setBackground(new Iron(false,false));
                LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,LinearLayout.LayoutParams.WRAP_CONTENT);lp.bottomMargin=12;list.addView(item,lp);
                item.setOnClickListener(v->{String full=path+"/"+name;try {
                    if(getContext().getAssets().list(full).length>0)showNotices(full);
                    else showNoticeText(full);
                }catch(IOException e){setNotice("Could not open notice.");}});
            }
        }catch(IOException e){setNotice("Could not list notices.");}
        footer(false);refresh();
    }
    private void showNoticeText(String path) {
        screen="notice:"+path;base("License");
        ScrollView scroll=place(page,new ScrollView(getContext()),80,210,1376,650);
        TextView text=new TextView(getContext());text.setTextColor(IVORY);text.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX,24);text.setTextIsSelectable(true);text.setPadding(24,24,24,24);text.setBackgroundColor(0xee101415);scroll.addView(text);
        try(InputStream in=getContext().getAssets().open(path);ByteArrayOutputStream out=new ByteArrayOutputStream()) {byte[] b=new byte[8192];int n;while((n=in.read(b))!=-1)out.write(b,0,n);text.setText(new String(out.toByteArray(),StandardCharsets.UTF_8));}
        catch(IOException e){text.setText("Could not open notice.");}
        footer(false);
    }
    static final class BattleLawIcon extends View {
        final int law;final boolean locked;final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);final Path path=new Path();
        BattleLawIcon(Context c,int law,boolean locked){super(c);this.law=law;this.locked=locked;setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);}
        void glyph(Canvas c,int icon,float x,float y,float scale,float rotation){c.save();c.translate(x,y);c.rotate(rotation);c.scale(scale,scale);RuneControlIcons.draw(c,icon,p,path);c.restore();}
        void mirrored(Canvas c,int icon,float x,float y,float scale){c.save();c.translate(x,y);c.scale(-scale,scale);RuneControlIcons.draw(c,icon,p,path);c.restore();}
        @Override protected void onDraw(Canvas c){
            c.save();c.scale(getWidth()/78f,getHeight()/78f);if(locked)c.saveLayerAlpha(0,0,78,78,110);
            p.setColor(0xff30291f);p.setStyle(Paint.Style.FILL);c.drawCircle(39,39,38,p);
            switch(law){
            case 0:glyph(c,RuneTouchLayout.KICK,39,39,.88f,0);break;
            case 1:glyph(c,RuneTouchLayout.BLOCK,39,39,.88f,0);break;
            case 2:glyph(c,RuneTouchLayout.ATTACK,25,39,.70f,-45);glyph(c,RuneTouchLayout.ATTACK,53,39,.70f,-45);break;
            case 3:glyph(c,RuneTouchLayout.ATTACK,39,39,.88f,0);mirrored(c,RuneTouchLayout.ATTACK,39,39,.88f);break;
            case 4:glyph(c,RuneTouchLayout.JUMP,39,39,.90f,0);break;
            case 5:glyph(c,RuneTouchLayout.BLOCK,31,39,.74f,0);glyph(c,RuneTouchLayout.JUMP,58,39,.45f,90);break;
            case 6:glyph(c,RuneControlIcons.HAMMER,39,34,.85f,0);glyph(c,RuneTouchLayout.JUMP,39,65,.34f,180);break;
            case 7:glyph(c,RuneControlIcons.CORPSE_LIFT,39,39,.88f,0);break;
            case 8:glyph(c,RuneTouchLayout.USE,39,39,.88f,0);break;
            case 9:glyph(c,RuneTouchLayout.DIVE,39,39,.88f,0);break;
            case 10:glyph(c,RuneTouchLayout.ATTACK,39,39,.88f,0);break;
            case 12:glyph(c,RuneTouchLayout.USE,30,32,.65f,0);glyph(c,RuneTouchLayout.JUMP,57,54,.45f,0);break;
            default:glyph(c,RuneTouchLayout.POWER,39,39,.88f,0);break;
            }
            if(locked)c.restore();c.restore();
        }
    }
    static final class ToggleArt extends Drawable {
        final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
        public void draw(Canvas c){
            boolean checked=false,focused=false;for(int state:getState()){if(state==android.R.attr.state_checked)checked=true;if(state==android.R.attr.state_focused)focused=true;}
            Rect b=getBounds();float y=b.centerY();
            p.setStyle(Paint.Style.FILL);p.setColor(checked?0xff614323:0xff272c2e);c.drawRoundRect(8,y-18,72,y+18,18,18,p);
            p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(2);p.setColor(checked?GOLD:0xff6a6d6e);c.drawRoundRect(8,y-18,72,y+18,18,18,p);
            p.setStyle(Paint.Style.FILL);p.setColor(checked?GOLD:0xff959793);c.drawCircle(checked?54:26,y,14,p);
            if(focused){p.setStyle(Paint.Style.STROKE);p.setColor(GOLD);c.drawRect(1,1,b.right-1,b.bottom-1,p);}
        }
        public boolean isStateful(){return true;}protected boolean onStateChange(int[] s){invalidateSelf();return true;}
        public void setAlpha(int a){}public void setColorFilter(ColorFilter f){}public int getOpacity(){return PixelFormat.TRANSLUCENT;}
    }
    static final class Iron extends Drawable {
        final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);final boolean gold,ornament,selected,interactive;
        Iron(boolean g,boolean o){this(g,o,false);}
        Iron(boolean g,boolean o,boolean s){this(g,o,s,true);}
        Iron(boolean g,boolean o,boolean s,boolean interactive){gold=g;ornament=o;selected=s;this.interactive=interactive;}
        public void draw(Canvas c) {
            Rect b=getBounds();boolean active=false;
            if(interactive)for(int state:getState())if(state==android.R.attr.state_pressed||state==android.R.attr.state_focused)active=true;
            boolean bright=gold||active||selected;
            paint.setStyle(Paint.Style.FILL);paint.setShader(new LinearGradient(b.left,b.top,b.right,b.bottom,gold?0xff825520:bright?0xde382a13:0xe9101415,gold?0xffc58c3e:bright?0xee1d180e:0xe90c0f10,Shader.TileMode.CLAMP));
            c.drawRect(b,paint);paint.setShader(null);paint.setStrokeWidth(bright?2:1);paint.setStyle(Paint.Style.STROKE);paint.setColor(bright?GOLD:ornament?0xff786044:0xff3e4140);
            c.drawRect(b.left+1,b.top+1,b.right-1,b.bottom-1,paint);
            if(ornament||gold)for(int i=0;i<4;i++) {c.save();c.translate(i%2==0?b.left:b.right,i<2?b.top:b.bottom);c.scale(i%2==0?1:-1,i<2?1:-1);Path p=new Path();p.moveTo(4,27);p.lineTo(17,14);p.lineTo(24,21);p.lineTo(17,28);p.lineTo(5,16);p.lineTo(16,5);p.lineTo(31,20);c.drawPath(p,paint);c.restore();}
            paint.setStyle(Paint.Style.FILL);
        }
        public boolean isStateful(){return true;}protected boolean onStateChange(int[] s){invalidateSelf();return true;}
        public void setAlpha(int a){}public void setColorFilter(ColorFilter f){}public int getOpacity(){return PixelFormat.TRANSLUCENT;}
    }
    final class Backdrop extends View {
        final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
        Backdrop(Context c){super(c);setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);}
        protected void onDraw(Canvas c){
            boolean home=screen.equals("pause")||screen.equals("title");
            Bitmap background=home||settingsArt==null?art:settingsArt;
            if(background!=null)c.drawBitmap(background,null,new Rect(0,0,1536,1024),p);
            if(!home){p.setColor(0x2505090a);c.drawRect(0,0,1536,1024,p);}
            p.setShader(new LinearGradient(0,0,home?700:1100,0,new int[]{0xe5000405,0x50030708,0x00000000},new float[]{0,.65f,1},Shader.TileMode.CLAMP));c.drawRect(0,0,1536,1024,p);p.setShader(null);
            p.setColor(0xff443a2b);p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(2);
            c.drawLine(10,0,10,938,p);c.drawLine(39,0,39,938,p);
            for(int y=-50;y<930;y+=96){
                Path knot=new Path();knot.moveTo(15,y);knot.lineTo(32,y+18);knot.lineTo(15,y+37);knot.lineTo(32,y+56);knot.lineTo(15,y+75);knot.lineTo(32,y+94);c.drawPath(knot,p);
                Path over=new Path();over.moveTo(31,y);over.lineTo(16,y+17);over.lineTo(31,y+34);over.lineTo(16,y+51);over.lineTo(31,y+68);over.lineTo(16,y+85);c.drawPath(over,p);
            }
            p.setStyle(Paint.Style.FILL);
        }
    }
    static final class Logo extends View {
        final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
        final Bitmap mark;
        Logo(Context c,Bitmap image){
            super(c);setContentDescription("Open Rune");
            mark=image;
        }
        protected void onDraw(Canvas c){
            c.save();c.scale(getWidth()/390f,getHeight()/110f);
            if(mark!=null)c.drawBitmap(mark,null,new RectF(0,0,110,110),p);
            p.setTypeface(Typeface.create("serif",Typeface.NORMAL));p.setTextSize(24);p.setColor(GOLD);
            c.drawText("OPEN",124,35,p);
            p.setTypeface(Typeface.create("serif",Typeface.BOLD));p.setTextSize(65);
            p.setShader(new LinearGradient(0,40,0,101,IVORY,0xffb6a17a,Shader.TileMode.CLAMP));
            c.drawText("RUNE",120,96,p);p.setShader(null);c.restore();
        }
    }
}
