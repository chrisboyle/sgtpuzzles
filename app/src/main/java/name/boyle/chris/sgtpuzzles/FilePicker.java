package name.boyle.chris.sgtpuzzles;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.text.MessageFormat;
import java.util.Arrays;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.os.Environment;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;
import android.widget.AdapterView.OnItemClickListener;

class FilePicker extends Dialog
{
	private String[] files;
	private ListView lv;
	private FilePicker parent;
	private final Activity activity;

	private void dismissAll()
	{
		try {
			activity.dismissDialog(0);
		} catch (Exception ignored) {}
		dismiss();
		FilePicker fp = this.parent;
		while (fp != null) {
			fp.dismiss();
			fp = fp.parent;
		}
	}

	private void load(final File f) throws IOException
	{
		byte[] b = new byte[(int)f.length()];
		RandomAccessFile raf = new RandomAccessFile(f,"r");
		raf.readFully(b);
		raf.close();
		Intent i = new Intent(getContext(), GamePlay.class);
		String savedGame = new String(b);
		if (savedGame.length() == 0) {
			throw new IOException("File is empty");
		}
		i.putExtra("game", savedGame);
		getContext().startActivity(i);
		dismissAll();
	}

	private void save(final File f, Boolean force)
	{
		if (! force && f.exists()) {
			AlertDialog.Builder b = new AlertDialog.Builder(getContext())
				.setMessage(R.string.file_exists)
				.setCancelable(true)
				.setIcon(android.R.drawable.ic_dialog_alert);
			b.setPositiveButton(android.R.string.yes, new OnClickListener(){ public void onClick(DialogInterface d, int which) {
				try {
					if (!f.delete()) {
						throw new RuntimeException("delete failed");
					}
					save(f, true);
				} catch (Exception e) {
					Toast.makeText(getContext(), e.toString(), Toast.LENGTH_LONG).show();
				}
				d.dismiss();
			}});
			b.setNegativeButton(android.R.string.no, new OnClickListener(){ public void onClick(DialogInterface d, int which) {
				d.cancel();
			}});
			b.show();
			return;
		}
		try {
			// FIXME return a result and move this into GamePlay
			String s = ((GamePlay)activity).saveToString();
			FileWriter w = new FileWriter(f);
			w.write(s,0,s.length());
			w.close();
			Toast.makeText(getContext(), MessageFormat.format(
					getContext().getString(R.string.file_saved),new Object[]{f.getPath()}),
					Toast.LENGTH_LONG).show();
			dismissAll();
		} catch (Exception e) {
			Toast.makeText(getContext(), e.toString(), Toast.LENGTH_LONG).show();
		}
	}

	FilePicker(Activity activity, final File path, final boolean isSave) { this(activity, path, isSave, null); }
	private FilePicker(Activity activity, final File path, final boolean isSave, FilePicker parent)
	{
		super(activity, android.R.style.Theme);  // full screen
		this.activity = activity;
		if( parent==null && ! Environment.getExternalStorageState().equals(Environment.MEDIA_MOUNTED)) {
			Toast.makeText(getContext(), R.string.storage_not_ready, Toast.LENGTH_SHORT).show();
			dismiss();
			return;
		}
		this.parent = parent;
		files = path.list();
		if (files == null) files = new String[0];  // TODO, this is probably permission denied, handle it better
		Arrays.sort(files);
		setTitle(path.getName());
		setCancelable(true);
		setContentView(isSave ? R.layout.file_save : R.layout.file_load);
		lv = (ListView)findViewById(R.id.filelist);
		lv.setAdapter(new ArrayAdapter<>(getContext(), android.R.layout.simple_list_item_1, files));
		lv.setOnItemClickListener(new OnItemClickListener() {
			public void onItemClick(AdapterView<?> arg0, View arg1, int which, long arg3) {
				File f = new File(path,files[which]);
				if (f.isDirectory()) {
					new FilePicker(FilePicker.this.activity, f,isSave,FilePicker.this).show();
					return;
				}
				if (isSave) {
					save(f, false);
					return;
				}
				try {
					if (f.length() > GamePlay.MAX_SAVE_SIZE) {
						Toast.makeText(getContext(), R.string.file_too_big, Toast.LENGTH_LONG).show();
						return;
					}
					load(f);
				} catch (Exception e) {
					Toast.makeText(getContext(), e.toString(), Toast.LENGTH_LONG).show();
				}
			}
		});
		if (!isSave) return;
		final EditText et = (EditText)findViewById(R.id.savebox);
		et.addTextChangedListener(new TextWatcher(){
			public void onTextChanged(CharSequence s,int a,int b, int c){}
			public void beforeTextChanged(CharSequence s,int a,int b, int c){}
			public void afterTextChanged(Editable s) {
				lv.setFilterText(s.toString());
			}
		});
		et.setOnEditorActionListener(new TextView.OnEditorActionListener() { public boolean onEditorAction(TextView v, int actionId, KeyEvent event) {
			Log.d(GamePlay.TAG,"actionId: "+actionId+", event: "+event);
			if (actionId == EditorInfo.IME_ACTION_DONE) return false;
			if ((event != null && event.getAction() != KeyEvent.ACTION_DOWN)
					|| et.length() == 0) return true;
			save(new File(path,et.getText().toString()), false);
			return true;
		}});
		final Button saveButton = (Button)findViewById(R.id.saveButton);
		saveButton.setOnClickListener(new View.OnClickListener(){public void onClick(View v){
			save(new File(path,et.getText().toString()), false);
		}});
		et.requestFocus();
	}
}
