package org.runeclassic.port;

import android.content.*;
import android.database.*;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import java.io.*;

/** Read-only, explicitly granted invitations; never exposes configs, saves or slot one. */
public final class RuneInvitationProvider extends ContentProvider {
    @Override public boolean onCreate(){return true;}
    private File file(Uri uri) throws FileNotFoundException {
        java.util.List<String> parts=uri.getPathSegments();
        if(parts.size()!=2||!parts.get(0).matches("[0-9a-f-]{36}")||!parts.get(1).matches("player([2-9]|1[0-6])\\.rune-invite"))throw new FileNotFoundException("Invalid invitation");
        File f=new File(getContext().getFilesDir(),"multiplayer/host/"+parts.get(0)+"/invitations/"+parts.get(1));
        if(!f.isFile())throw new FileNotFoundException("Invitation is no longer available");return f;
    }
    @Override public ParcelFileDescriptor openFile(Uri uri,String mode) throws FileNotFoundException {
        if(!mode.equals("r"))throw new FileNotFoundException("Read only");return ParcelFileDescriptor.open(file(uri),ParcelFileDescriptor.MODE_READ_ONLY);
    }
    @Override public String getType(Uri uri){return "application/octet-stream";}
    @Override public Cursor query(Uri uri,String[] projection,String selection,String[] args,String sort){
        try{
            File f=file(uri);String[] columns=projection==null?new String[]{OpenableColumns.DISPLAY_NAME,OpenableColumns.SIZE}:projection;
            MatrixCursor cursor=new MatrixCursor(columns);Object[] row=new Object[columns.length];
            for(int i=0;i<columns.length;i++)row[i]=OpenableColumns.DISPLAY_NAME.equals(columns[i])?f.getName():OpenableColumns.SIZE.equals(columns[i])?f.length():null;
            cursor.addRow(row);return cursor;
        }catch(FileNotFoundException e){return null;}
    }
    @Override public Uri insert(Uri uri,ContentValues values){throw new UnsupportedOperationException();}
    @Override public int delete(Uri uri,String selection,String[] args){throw new UnsupportedOperationException();}
    @Override public int update(Uri uri,ContentValues values,String selection,String[] args){throw new UnsupportedOperationException();}
}
