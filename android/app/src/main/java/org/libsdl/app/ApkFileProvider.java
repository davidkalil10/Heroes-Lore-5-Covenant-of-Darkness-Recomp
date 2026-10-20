package org.libsdl.app;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import java.io.File;
import java.io.FileNotFoundException;

public class ApkFileProvider extends ContentProvider {
    @Override
    public boolean onCreate() {
        return true;
    }

    private File resolveApk(Uri uri) throws FileNotFoundException {
        try {
            File allowed = new File(getContext().getFilesDir(), "data/updates").getCanonicalFile();
            File file = new File(uri.getPath()).getCanonicalFile();
            if (!file.getPath().startsWith(allowed.getPath() + File.separator) || !file.getName().endsWith(".apk") || !file.isFile()) {
                throw new FileNotFoundException("Invalid update APK");
            }
            return file;
        } catch (java.io.IOException error) {
            throw new FileNotFoundException("Cannot resolve update APK");
        }
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (!"r".equals(mode)) throw new FileNotFoundException("Read only");
        File file = resolveApk(uri);
        if (file.exists()) {
            return ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY);
        }
        throw new FileNotFoundException("Arquivo APK nao encontrado: " + uri.getPath());
    }

    @Override
    public String getType(Uri uri) {
        return "application/vnd.android.package-archive";
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) {
        File file;
        try { file = resolveApk(uri); } catch (FileNotFoundException error) { return null; }
        String[] cols = projection != null ? projection : new String[]{"_display_name", "_size"};
        MatrixCursor cursor = new MatrixCursor(cols, 1);
        MatrixCursor.RowBuilder row = cursor.newRow();
        for (String col : cols) {
            if ("_display_name".equals(col)) {
                row.add(file.getName());
            } else if ("_size".equals(col)) {
                row.add(file.length());
            } else {
                row.add(null);
            }
        }
        return cursor;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        return null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        return 0;
    }
}
