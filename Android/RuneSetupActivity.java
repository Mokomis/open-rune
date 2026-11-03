package org.runeclassic.port;

import android.app.Activity;
import android.content.*;
import android.database.Cursor;
import android.graphics.*;
import android.graphics.drawable.*;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.util.*;

/** Runs without SDL or game data; only the system picker receives folder access. */
public final class RuneSetupActivity extends Activity {
    private TextView status;private Button choose,buy;private boolean copying,launched;
    private static final int PICK=410;
    @Override public void onCreate(Bundle state){
        super.onCreate(state);getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if(RuneGameFiles.ready(new File(getFilesDir(),"game"))){launch();return;}
        FrameLayout backdrop=new FrameLayout(this);backdrop.setBackgroundColor(0xff101314);
        try(InputStream in=getAssets().open("menu-background.png")){Bitmap image=BitmapFactory.decodeStream(in);if(image!=null)backdrop.setBackground(new BitmapDrawable(getResources(),image));}catch(IOException ignored){}
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.setBackgroundColor(0xcc101314);backdrop.addView(scroll,new FrameLayout.LayoutParams(-1,-1));
        LinearLayout content=new LinearLayout(this);content.setOrientation(LinearLayout.VERTICAL);content.setGravity(Gravity.CENTER_VERTICAL);content.setPadding(dp(40),dp(24),dp(40),dp(24));scroll.addView(content,new ScrollView.LayoutParams(-1,-1));
        text(content,"OPEN RUNE",30,0xffedbd68);text(content,"Set up your game files",24,0xffeee8db);
        text(content,"Open Rune requires the original Rune game files. Select your Rune Classic folder, or get the game on Steam.",17,0xffeee8db);
        choose=button(content,"Use an existing Rune installation",()->pick());
        buy=button(content,"Buy Rune Classic on Steam",()->{
            try{startActivity(new Intent(Intent.ACTION_VIEW,Uri.parse(RuneGameFiles.STEAM)));}catch(ActivityNotFoundException e){status.setText("No browser is available. Visit store.steampowered.com/app/210950");}
        });
        status=text(content,"On Android, copy your Rune Classic folder to this device first, then select it here. It must contain System, Maps, Textures, Meshes, Sounds and Music.\n\nRequired files are copied into Open Rune's private storage. Original files and saves stay unchanged.",15,0xffbbb9af);
        setContentView(backdrop);
    }
    private int dp(int n){return Math.round(n*getResources().getDisplayMetrics().density);}
    private TextView text(LinearLayout parent,String value,int size,int color){TextView t=new TextView(this);t.setText(value);t.setTextSize(size);t.setTextColor(color);t.setTypeface(Typeface.create("sans-serif",Typeface.NORMAL));t.setPadding(0,dp(6),0,dp(10));parent.addView(t);return t;}
    private Button button(LinearLayout parent,String title,Runnable action){Button b=new Button(this);b.setText(title);b.setAllCaps(false);b.setTextColor(0xffeee8db);b.setTextSize(18);GradientDrawable d=new GradientDrawable();d.setColor(0xee1b2224);d.setStroke(dp(1),0xffaf8d50);d.setCornerRadius(dp(4));b.setBackground(d);b.setPadding(dp(16),dp(8),dp(16),dp(8));LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,dp(58));p.setMargins(0,dp(8),0,dp(8));parent.addView(b,p);b.setOnClickListener(v->action.run());return b;}
    private void launch(){if(launched)return;launched=true;startActivity(new Intent(this,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP|Intent.FLAG_ACTIVITY_SINGLE_TOP));finish();}
    private void pick(){
        Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try{startActivityForResult(intent,PICK);}catch(ActivityNotFoundException e){status.setText("No folder picker is available on this device.");}
    }
    private static final class Document {String id,name;boolean directory;Document(String id,String name,boolean directory){this.id=id;this.name=name;this.directory=directory;}}
    private List<Document> children(Uri tree,String id)throws IOException {
        ArrayList<Document> out=new ArrayList<>();
        Uri list=DocumentsContract.buildChildDocumentsUriUsingTree(tree,id);
        try(Cursor cursor=getContentResolver().query(list,new String[]{DocumentsContract.Document.COLUMN_DOCUMENT_ID,DocumentsContract.Document.COLUMN_DISPLAY_NAME,DocumentsContract.Document.COLUMN_MIME_TYPE},null,null,null)){
            if(cursor==null)throw new IOException("Could not read the selected folder.");
            while(cursor.moveToNext()){
                if(out.size()>=20000)throw new IOException("Too many entries in the selected folder.");
                out.add(new Document(cursor.getString(0),cursor.getString(1),DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(2))));
            }
        }return out;
    }
    @Override public void onActivityResult(int request,int result,Intent data){
        super.onActivityResult(request,result,data);if(request!=PICK||result!=RESULT_OK||data==null||data.getData()==null)return;
        if(copying)return;copying=true;choose.setEnabled(false);buy.setEnabled(false);status.setText("Preparing to copy game files…");
        Uri tree=data.getData();
        new Thread(()->{
            File stage=null;String error=null;
            try{
                File destination=new File(getFilesDir(),"game");if(destination.exists())throw new IOException("Open Rune's data folder already exists and was left unchanged. The existing files need repair before a new import.");
                stage=java.nio.file.Files.createTempDirectory(getFilesDir().toPath(),"rune-import-").toFile();
                List<Document> roots=children(tree,DocumentsContract.getTreeDocumentId(tree));
                int count=0;
                for(String folder:RuneGameFiles.FOLDERS){
                    Document match=null;for(Document document:roots)if(document.directory&&folder.equalsIgnoreCase(document.name)){if(match!=null)throw new IOException("Duplicate game folder: "+folder);match=document;}
                    if(match==null)throw new IOException("Missing "+folder+". Select the Rune Classic folder containing all six game folders.");
                    File target=new File(stage,folder);if(!target.mkdir())throw new IOException("Could not create import folder.");Set<String> names=new HashSet<>();
                    for(Document doc:children(tree,match.id)){
                        if(doc.directory||!RuneGameFiles.allowed(folder,doc.name))continue;
                        String name=RuneGameFiles.canonical(doc.name);if(!names.add(RuneGameFiles.lower(name)))throw new IOException("Duplicate file name in "+folder+": "+name);
                        Uri uri=DocumentsContract.buildDocumentUriUsingTree(tree,doc.id);
                        try(InputStream input=getContentResolver().openInputStream(uri);FileOutputStream output=new FileOutputStream(new File(target,name))){
                            if(input==null)throw new IOException("Could not read "+name);byte[] bytes=new byte[262144];int n;
                            while((n=input.read(bytes))!=-1){if(stage.getUsableSpace()<n+8*1024*1024)throw new IOException("Not enough free storage to copy the game files.");output.write(bytes,0,n);}output.getFD().sync();
                        }
                        final int imported=++count;final String current=folder+"/"+name;runOnUiThread(()->status.setText("Copying game files · "+imported+" files\n"+current));
                    }
                }
                RuneGameFiles.prepare(stage);
                if(destination.exists()||!stage.renameTo(destination))throw new IOException("Could not finish the import. Existing files were left unchanged.");stage=null;
            }catch(Exception e){error=e.getMessage()==null?"Could not import the selected game files.":e.getMessage();}
            finally{if(stage!=null)try{RuneGameFiles.removeStage(stage);}catch(IOException ignored){}}
            final String failure=error;runOnUiThread(()->{copying=false;if(isFinishing()||isDestroyed())return;if(failure==null)launch();else{status.setText(failure);choose.setEnabled(true);buy.setEnabled(true);}});
        },"Rune game-file import").start();
    }
    @Override public void onBackPressed(){if(copying)Toast.makeText(this,"Please wait for the game-file import to finish.",Toast.LENGTH_SHORT).show();else super.onBackPressed();}
}
