package name.boyle.chris.sgtpuzzles;

public class MenuEntry {
	private final int id;
	private final String title;
	private final MenuEntry[] submenu;
	private final String params;

	@UsedByJNI
	public MenuEntry(final int id, final String title, final MenuEntry[] submenu) {
		this.id = id;
		this.title = title;
		this.params = null;
		this.submenu = submenu;
	}

	@UsedByJNI
	public MenuEntry(final int id, final String title, final String params) {
		this.id = id;
		this.title = title;
		this.params = params;
		this.submenu = null;
	}

	public int getId() {
		return id;
	}

	public String getTitle() {
		return title;
	}

	public String getParams() {
		return params;
	}

	public MenuEntry[] getSubmenu() {
		return submenu;
	}
}
