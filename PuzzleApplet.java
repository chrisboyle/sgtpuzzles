/*
 * PuzzleApplet.java: NestedVM applet for the puzzle collection
 */
import java.awt.*;
import java.awt.event.*;
import java.awt.image.BufferedImage;
import java.util.*;
import javax.swing.*;
import javax.swing.border.BevelBorder;
import javax.swing.Timer;
import java.util.List;

import org.ibex.nestedvm.Runtime;

public class PuzzleApplet extends JApplet implements Runtime.CallJavaCB {

    private static final long serialVersionUID = 1L;

    private static final int CFG_SETTINGS = 0, CFG_SEED = 1, CFG_DESC = 2,
            LEFT_BUTTON = 0x0200, MIDDLE_BUTTON = 0x201, RIGHT_BUTTON = 0x202,
            LEFT_DRAG = 0x203, MIDDLE_DRAG = 0x204, RIGHT_DRAG = 0x205,
            LEFT_RELEASE = 0x206, CURSOR_UP = 0x209, CURSOR_DOWN = 0x20a,
            CURSOR_LEFT = 0x20b, CURSOR_RIGHT = 0x20c, MOD_CTRL = 0x1000,
            MOD_SHFT = 0x2000, MOD_NUM_KEYPAD = 0x4000, ALIGN_VCENTRE = 0x100,
            ALIGN_HCENTRE = 0x001, ALIGN_HRIGHT = 0x002, C_STRING = 0,
            C_CHOICES = 1, C_BOOLEAN = 2;

    private JFrame mainWindow;

    private JMenu typeMenu;
    private JMenuItem[] typeMenuItems;
    private int customMenuItemIndex;

    private JMenuItem solveCommand;
    private Color[] colors;
    private JLabel statusBar;
    private PuzzlePanel pp;
    private Runtime runtime;
    private String[] puzzle_args;
    private Graphics2D  gg;
    private Timer timer;
    private int xarg1, xarg2, xarg3;
    private int[] xPoints, yPoints;
    private BufferedImage[] blitters = new BufferedImage[512];
    private ConfigDialog dlg;

    static {
        try {
            UIManager.setLookAndFeel(UIManager.getSystemLookAndFeelClassName());
        } catch (Exception ex) {
            ex.printStackTrace();
        }
    }

    public void init() {
        try {
            Container cp = getContentPane();
            cp.setLayout(new BorderLayout());
            runtime = (Runtime) Class.forName("PuzzleEngine").newInstance();
            runtime.setCallJavaCB(this);
            JMenuBar menubar = new JMenuBar();
            JMenu jm;
            menubar.add(jm = new JMenu("Game"));
            addMenuItemCallback(jm, "New", "jcallback_newgame_event");
            addMenuItemCallback(jm, "Restart", "jcallback_restart_event");
            addMenuItemCallback(jm, "Specific...", "jcallback_config_event", CFG_DESC);
            addMenuItemCallback(jm, "Random Seed...", "jcallback_config_event", CFG_SEED);
            jm.addSeparator();
            addMenuItemCallback(jm, "Undo", "jcallback_undo_event");
            addMenuItemCallback(jm, "Redo", "jcallback_redo_event");
            jm.addSeparator();
            solveCommand = addMenuItemCallback(jm, "Solve", "jcallback_solve_event");
            solveCommand.setEnabled(false);
            if (mainWindow != null) {
                jm.addSeparator();
                addMenuItemCallback(jm, "Exit", "jcallback_quit_event");
            }
            menubar.add(typeMenu = new JMenu("Type"));
            typeMenu.setVisible(false);
            menubar.add(jm = new JMenu("Help"));
            addMenuItemCallback(jm, "About", "jcallback_about_event");
            setJMenuBar(menubar);
            cp.add(pp = new PuzzlePanel(), BorderLayout.CENTER);
            pp.addKeyListener(new KeyAdapter() {
                public void keyPressed(KeyEvent e) {
                    int key = -1;
                    int shift = e.isShiftDown() ? MOD_SHFT : 0;
                    int ctrl = e.isControlDown() ? MOD_CTRL : 0;
                    switch (e.getKeyCode()) {
                    case KeyEvent.VK_LEFT:
                    case KeyEvent.VK_KP_LEFT:
                        key = shift | ctrl | CURSOR_LEFT;
                        break;
                    case KeyEvent.VK_RIGHT:
                    case KeyEvent.VK_KP_RIGHT:
                        key = shift | ctrl | CURSOR_RIGHT;
                        break;
                    case KeyEvent.VK_UP:
                    case KeyEvent.VK_KP_UP:
                        key = shift | ctrl | CURSOR_UP;
                        break;
                    case KeyEvent.VK_DOWN:
                    case KeyEvent.VK_KP_DOWN:
                        key = shift | ctrl | CURSOR_DOWN;
                        break;
                    case KeyEvent.VK_PAGE_UP:
                        key = shift | ctrl | MOD_NUM_KEYPAD | '9';
                        break;
                    case KeyEvent.VK_PAGE_DOWN:
                        key = shift | ctrl | MOD_NUM_KEYPAD | '3';
                        break;
                    case KeyEvent.VK_HOME:
                        key = shift | ctrl | MOD_NUM_KEYPAD | '7';
                        break;
                    case KeyEvent.VK_END:
                        key = shift | ctrl | MOD_NUM_KEYPAD | '1';
                        break;
                    default:
                        if (e.getKeyCode() >= KeyEvent.VK_NUMPAD0 && e.getKeyCode() <=KeyEvent.VK_NUMPAD9) {
                            key = MOD_NUM_KEYPAD | (e.getKeyCode() - KeyEvent.VK_NUMPAD0+'0');
                        }
                    break;
                    }
                    if (key != -1) {
                        runtimeCall("jcallback_key_event", new int[] {0, 0, key});
                    }
                }
                public void keyTyped(KeyEvent e) {
                    int key = e.getKeyChar();
                    if (key == 26 && e.isShiftDown() && e.isControlDown()) {
                        runtimeCall("jcallback_redo_event", new int[0]);
                        return;
                    }
                    runtimeCall("jcallback_key_event", new int[] {0, 0, key});
                }
            });
            pp.addMouseListener(new MouseAdapter() {
                public void mouseReleased(MouseEvent e) {
                    mousePressedReleased(e, true);
                }
                public void mousePressed(MouseEvent e) {
                    pp.requestFocus();
                    mousePressedReleased(e, false);
                }
                private void mousePressedReleased(MouseEvent e, boolean released) {
                    int button;
                    if ((e.getModifiers() & (InputEvent.BUTTON2_MASK | InputEvent.SHIFT_MASK)) != 0)
                        button = MIDDLE_BUTTON;
                    else if ((e.getModifiers() & (InputEvent.BUTTON3_MASK | InputEvent.ALT_MASK)) != 0)
                        button = RIGHT_BUTTON;
                    else if ((e.getModifiers() & (InputEvent.BUTTON1_MASK)) != 0)
                        button = LEFT_BUTTON;
                    else
                        return;
                    if (released)
                        button += LEFT_RELEASE - LEFT_BUTTON;
                    runtimeCall("jcallback_key_event", new int[] {e.getX(), e.getY(), button});
                }
            });
            pp.addMouseMotionListener(new MouseMotionAdapter() {
                public void mouseDragged(MouseEvent e) {
                    int button;
                    if ((e.getModifiers() & (InputEvent.BUTTON2_MASK | InputEvent.SHIFT_MASK)) != 0)
                        button = MIDDLE_DRAG;
                    else if ((e.getModifiers() & (InputEvent.BUTTON3_MASK | InputEvent.ALT_MASK)) != 0)
                        button = RIGHT_DRAG;
                    else
                        button = LEFT_DRAG;
                    runtimeCall("jcallback_key_event", new int[] {e.getX(), e.getY(), button});
                }
            });
            pp.addComponentListener(new ComponentAdapter() {
                public void componentResized(ComponentEvent e) {
                    handleResized();
                }
            });
            pp.setFocusable(true);
            pp.requestFocus();
            timer = new Timer(20, new ActionListener() {
                public void actionPerformed(ActionEvent e) {
                    runtimeCall("jcallback_timer_func", new int[0]);
                }
            });
	    String gameid;
	    try {
		gameid = getParameter("game_id");
	    } catch (java.lang.NullPointerException ex) {
		gameid = null;
	    }
	    if (gameid == null) {
		puzzle_args = null;
	    } else {
		puzzle_args = new String[2];
		puzzle_args[0] = "puzzle";
		puzzle_args[1] = gameid;
	    }
            SwingUtilities.invokeLater(new Runnable() {
                public void run() {
                    runtime.start(puzzle_args);
                    runtime.execute();
                }
            });
        } catch (Exception ex) {
            ex.printStackTrace();
        }
    }

    public void destroy() {
        SwingUtilities.invokeLater(new Runnable() {
            public void run() {
                runtime.execute();
                if (mainWindow != null) {
                    mainWindow.dispose();
                    System.exit(0);
                }
            }
        });
    }

    protected void handleResized() {
        pp.createBackBuffer(pp.getWidth(), pp.getHeight(), colors[0]);
        runtimeCall("jcallback_resize", new int[] {pp.getWidth(), pp.getHeight()});
    }

    private JMenuItem addMenuItemCallback(JMenu jm, String name, final String callback, final int arg) {
        return addMenuItemCallback(jm, name, callback, new int[] {arg}, false);
    }

    private JMenuItem addMenuItemCallback(JMenu jm, String name, final String callback) {
        return addMenuItemCallback(jm, name, callback, new int[0], false);
    }

    private JMenuItem addMenuItemCallback(JMenu jm, String name, final String callback, final int[] args, boolean checkbox) {
        JMenuItem jmi;
        if (checkbox)
            jm.add(jmi = new JCheckBoxMenuItem(name));
        else
        jm.add(jmi = new JMenuItem(name));
        jmi.addActionListener(new ActionListener() {
            public void actionPerformed(ActionEvent e) {
                runtimeCall(callback, args);
            }
        });
        return jmi;
    }

    protected void runtimeCall(String func, int[] args) {
        if (runtimeCallWithResult(func, args) == 42 && mainWindow != null) {
            destroy();
        }
    }

    protected int runtimeCallWithResult(String func, int[] args) {
        try {
            return runtime.call(func, args);
        } catch (Runtime.CallException ex) {
            ex.printStackTrace();
            return 42;
        }
    }

    private void buildConfigureMenuItem() {
        if (typeMenu.isVisible()) {
            typeMenu.addSeparator();
        } else {
            typeMenu.setVisible(true);
        }
        typeMenuItems[customMenuItemIndex] =
            addMenuItemCallback(typeMenu, "Custom...",
                                "jcallback_config_event",
                                new int[] {CFG_SETTINGS}, true);
    }

    private void addTypeItem
        (JMenu targetMenu, String name, int newId, final int ptrGameParams) {

        typeMenu.setVisible(true);
        typeMenuItems[newId] =
            addMenuItemCallback(targetMenu, name,
                                "jcallback_preset_event",
                                new int[] {ptrGameParams}, true);
    }

    private void addTypeSubmenu
        (JMenu targetMenu, String name, int newId) {

        JMenu newMenu = new JMenu(name);
        newMenu.setVisible(true);
        typeMenuItems[newId] = newMenu;
        targetMenu.add(newMenu);
    }

    public int call(int cmd, int arg1, int arg2, int arg3) {
        try {
            switch(cmd) {
            case 0: // initialize
                if (mainWindow != null) mainWindow.setTitle(runtime.cstring(arg1));
                if ((arg2 & 1) != 0) buildConfigureMenuItem();
                if ((arg2 & 2) != 0) addStatusBar();
                if ((arg2 & 4) != 0) solveCommand.setEnabled(true);
                colors = new Color[arg3];
                return 0;
            case 1: // configure Type menu
                if (arg1 == 0) {
                    // preliminary setup
                    typeMenuItems = new JMenuItem[arg2 + 2];
                    typeMenuItems[arg2] = typeMenu;
                    customMenuItemIndex = arg2 + 1;
                    return arg2;
                } else if (xarg1 != 0) {
                    addTypeItem((JMenu)typeMenuItems[arg2],
                                runtime.cstring(arg1), arg3, xarg1);
                } else {
                    addTypeSubmenu((JMenu)typeMenuItems[arg2],
                                   runtime.cstring(arg1), arg3);
                }
                return 0;
            case 2: // MessageBox
                JOptionPane.showMessageDialog(this, runtime.cstring(arg2), runtime.cstring(arg1), arg3 == 0 ? JOptionPane.INFORMATION_MESSAGE : JOptionPane.ERROR_MESSAGE);
                return 0;
            case 3: // Resize
                pp.setPreferredSize(new Dimension(arg1, arg2));
                if (mainWindow != null) mainWindow.pack();
                handleResized();
                if (mainWindow != null) mainWindow.setVisible(true);
                return 0;
            case 4: // drawing tasks
                switch(arg1) {
                case 0:
		    String text = runtime.cstring(arg2);
		    if (text.equals("")) text = " ";
		    statusBar.setText(text);
		    break;
                case 1:
                    gg = pp.backBuffer.createGraphics();
                    if (arg2 != 0 || arg3 != 0 ||
			arg2 + xarg2 != getWidth() ||
			arg3 + xarg3 != getHeight()) {
			int left = arg2, right = arg2 + xarg2;
			int top = arg3, bottom = arg3 + xarg3;
			int width = getWidth(), height = getHeight();
                        gg.setColor(colors != null ? colors[0] : Color.black);
                        gg.fillRect(0, 0, left, height);
                        gg.fillRect(right, 0, width-right, height);
                        gg.fillRect(0, 0, width, top);
                        gg.fillRect(0, bottom, width, height-bottom);
                        gg.setClip(left, top, right-left, bottom-top);
                    }
                    break;
                case 2: gg.dispose(); pp.repaint(); break;
                case 3: gg.setClip(arg2, arg3, xarg1, xarg2); break;
                case 4:
                    if (arg2 == 0 && arg3 == 0) {
                        gg.setClip(0, 0, getWidth(), getHeight());
                    } else {
                        gg.setClip(arg2, arg3, getWidth()-2*arg2, getHeight()-2*arg3);
                    }
                    break;
                case 5:
                    gg.setColor(colors[xarg3]);
                    gg.fillRect(arg2, arg3, xarg1, xarg2);
                    break;
                case 6:
                    gg.setColor(colors[xarg3]);
                    gg.drawLine(arg2, arg3, xarg1, xarg2);
                    break;
                case 7:
                    xPoints = new int[arg2];
                    yPoints = new int[arg2];
                    break;
                case 8:
                    if (arg3 != -1) {
                        gg.setColor(colors[arg3]);
                        gg.fillPolygon(xPoints, yPoints, xPoints.length);
                    }
                    gg.setColor(colors[arg2]);
                    gg.drawPolygon(xPoints, yPoints, xPoints.length);
                    break;
                case 9:
                    if (arg3 != -1) {
                        gg.setColor(colors[arg3]);
                        gg.fillOval(xarg1-xarg3, xarg2-xarg3, xarg3*2, xarg3*2);
                    }
                    gg.setColor(colors[arg2]);
                    gg.drawOval(xarg1-xarg3, xarg2-xarg3, xarg3*2, xarg3*2);
                    break;
                case 10:
                    for(int i=0; i<blitters.length; i++) {
                        if (blitters[i] == null) {
                            blitters[i] = new BufferedImage(arg2, arg3, BufferedImage.TYPE_3BYTE_BGR);
                            return i;
                        }
                    }
                    throw new RuntimeException("No free blitter found!");
                case 11: blitters[arg2] = null; break;
                case 12:
                    timer.start(); break;
                case 13:
                    timer.stop(); break;
                }
                return 0;
            case 5: // more arguments
                xarg1 = arg1;
                xarg2 = arg2;
                xarg3 = arg3;
                return 0;
            case 6: // polygon vertex
                xPoints[arg1]=arg2;
                yPoints[arg1]=arg3;
                return 0;
            case 7: // string
                gg.setColor(colors[arg2]);
                {
                    String text = runtime.utfstring(arg3);
                    Font ft = new Font((xarg3 & 0x10) != 0 ? "Monospaced" : "Dialog",
                            Font.PLAIN, 100);
                    int height100 = this.getFontMetrics(ft).getHeight();
                    ft = ft.deriveFont(arg1 * 100 / (float)height100);
                    FontMetrics fm = this.getFontMetrics(ft);
                    int asc = fm.getAscent(), desc = fm.getDescent();
                    if ((xarg3 & ALIGN_VCENTRE) != 0)
                        xarg2 += asc - (asc+desc)/2;
                    int wid = fm.stringWidth(text);
                    if ((xarg3 & ALIGN_HCENTRE) != 0)
                        xarg1 -= wid / 2;
                    else if ((xarg3 & ALIGN_HRIGHT) != 0)
                        xarg1 -= wid;
                    gg.setFont(ft);
                    gg.drawString(text, xarg1, xarg2);
                }
                return 0;
            case 8: // blitter_save
                Graphics g2 = blitters[arg1].createGraphics();
                g2.drawImage(pp.backBuffer, 0, 0, blitters[arg1].getWidth(), blitters[arg1].getHeight(),
                        arg2, arg3, arg2 + blitters[arg1].getWidth(), arg3 + blitters[arg1].getHeight(), this);
                g2.dispose();
                return 0;
            case 9: // blitter_load
                gg.drawImage(blitters[arg1], arg2, arg3, this);
                return 0;
            case 10: // dialog_init
                dlg= new ConfigDialog(this, runtime.cstring(arg1));
                return 0;
            case 11: // dialog_add_control
                {
                    int sval_ptr = arg1;
                    int ival = arg2;
                    int ptr = xarg1;
                    int type=xarg2;
                    String name = runtime.cstring(xarg3);
                    switch(type) {
                    case C_STRING:
                        dlg.addTextBox(ptr, name, runtime.cstring(sval_ptr));
                        break;
                    case C_BOOLEAN:
                        dlg.addCheckBox(ptr, name, ival != 0);
                        break;
                    case C_CHOICES:
                        dlg.addComboBox(ptr, name, runtime.cstring(sval_ptr), ival);
                    }
                }
                return 0;
            case 12:
                dlg.finish();
                dlg = null;
                return 0;
            case 13: // tick a menu item
                if (arg1 < 0) arg1 = customMenuItemIndex;
                for (int i = 0; i < typeMenuItems.length; i++) {
                    if (typeMenuItems[i] instanceof JCheckBoxMenuItem) {
                        ((JCheckBoxMenuItem)typeMenuItems[i]).setSelected
                            (arg1 == i);
                    }
                }
                return 0;
            default:
                if (cmd >= 1024 && cmd < 2048) { // palette
                    colors[cmd-1024] = new Color(arg1, arg2, arg3);
                }
            if (cmd == 1024) {
                pp.setBackground(colors[0]);
                if (statusBar != null) statusBar.setBackground(colors[0]);
                this.setBackground(colors[0]);
            }
            return 0;
            }
        } catch (Throwable ex) {
            ex.printStackTrace();
            System.exit(-1);
            return 0;
        }
    }

    private void addStatusBar() {
        statusBar = new JLabel("test");
        statusBar.setBorder(new BevelBorder(BevelBorder.LOWERED));
        getContentPane().add(BorderLayout.SOUTH,statusBar);
    }

    // Standalone runner
    public static void main(String[] args) {
        final PuzzleApplet a = new PuzzleApplet();
        JFrame jf = new JFrame("Loading...");
        jf.getContentPane().setLayout(new BorderLayout());
        jf.getContentPane().add(a, BorderLayout.CENTER);
        a.mainWindow=jf;
        a.init();
        a.start();
        jf.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        jf.addWindowListener(new WindowAdapter() {
            public void windowClosing(WindowEvent e) {
                a.stop();
                a.destroy();
            }
        });
        jf.setVisible(true);
    }

    public static class PuzzlePanel extends JPanel {

        private static final long serialVersionUID = 1L;
        protected BufferedImage backBuffer;

        public PuzzlePanel() {
            setPreferredSize(new Dimension(100,100));
            createBackBuffer(100,100, Color.black);
        }

        public void createBackBuffer(int w, int h, Color bg) {
	    if (w > 0 && h > 0) {
		backBuffer = new BufferedImage(w,h, BufferedImage.TYPE_3BYTE_BGR);
		Graphics g = backBuffer.createGraphics();
		g.setColor(bg);
		g.fillRect(0, 0, w, h);
		g.dispose();
	    }
        }

        protected void paintComponent(Graphics g) {
            g.drawImage(backBuffer, 0, 0, this);
        }
    }

    public static class ConfigComponent {
        public int type;
        public int configItemPointer;
        public JComponent component;

        public ConfigComponent(int type, int configItemPointer, JComponent component) {
            this.type = type;
            this.configItemPointer = configItemPointer;
            this.component = component;
        }
    }

    public class ConfigDialog extends JDialog {

        private GridBagConstraints gbcLeft = new GridBagConstraints(
                GridBagConstraints.RELATIVE, GridBagConstraints.RELATIVE, 1, 1,
                0, 0, GridBagConstraints.WEST, GridBagConstraints.NONE,
                new Insets(0, 0, 0, 0), 0, 0);
        private GridBagConstraints gbcRight = new GridBagConstraints(
                GridBagConstraints.RELATIVE, GridBagConstraints.RELATIVE,
                GridBagConstraints.REMAINDER, 1, 1.0, 0,
                GridBagConstraints.CENTER, GridBagConstraints.HORIZONTAL,
                new Insets(5, 5, 5, 5), 0, 0);
        private GridBagConstraints gbcBottom = new GridBagConstraints(
                GridBagConstraints.RELATIVE, GridBagConstraints.RELATIVE,
                GridBagConstraints.REMAINDER, GridBagConstraints.REMAINDER,
                1.0, 1.0, GridBagConstraints.CENTER,
                GridBagConstraints.HORIZONTAL, new Insets(5, 5, 5, 5), 0, 0);

        private static final long serialVersionUID = 1L;
        private List components = new ArrayList();

        public ConfigDialog(JApplet parent, String title) {
            super(JOptionPane.getFrameForComponent(parent), title, true);
            getContentPane().setLayout(new GridBagLayout());
        }

        public void addTextBox(int ptr, String name, String value) {
            getContentPane().add(new JLabel(name), gbcLeft);
            JComponent c = new JTextField(value, 25);
            getContentPane().add(c, gbcRight);
            components.add(new ConfigComponent(C_STRING, ptr, c));
        }


        public void addCheckBox(int ptr, String name, boolean selected) {
            JComponent c = new JCheckBox(name, selected);
            getContentPane().add(c, gbcRight);
            components.add(new ConfigComponent(C_BOOLEAN, ptr, c));
        }

        public void addComboBox(int ptr, String name, String values, int selected) {
            getContentPane().add(new JLabel(name), gbcLeft);
            StringTokenizer st = new StringTokenizer(values.substring(1), values.substring(0,1));
            JComboBox c = new JComboBox();
            c.setEditable(false);
            while(st.hasMoreTokens())
                c.addItem(st.nextToken());
            c.setSelectedIndex(selected);
            getContentPane().add(c, gbcRight);
            components.add(new ConfigComponent(C_CHOICES, ptr, c));
        }

        public void finish() {
            JPanel buttons = new JPanel(new GridLayout(1, 2, 5, 5));
            getContentPane().add(buttons, gbcBottom);
            JButton b;
            buttons.add(b=new JButton("OK"));
            b.addActionListener(new ActionListener() {
                public void actionPerformed(ActionEvent e) {
                    save();
                    dispose();
                }
            });
            getRootPane().setDefaultButton(b);
            buttons.add(b=new JButton("Cancel"));
            b.addActionListener(new ActionListener() {
                public void actionPerformed(ActionEvent e) {
                    dispose();
                }
            });
            setDefaultCloseOperation(DISPOSE_ON_CLOSE);
            pack();
            setLocationRelativeTo(null);
            setVisible(true);
        }
        private void save() {
            for (int i = 0; i < components.size(); i++) {
                ConfigComponent cc = (ConfigComponent) components.get(i);
                switch(cc.type) {
                case C_STRING:
                    JTextField jtf = (JTextField)cc.component;
                    runtimeCall("jcallback_config_set_string", new int[] {cc.configItemPointer, runtime.strdup(jtf.getText())});
                    break;
                case C_BOOLEAN:
                    JCheckBox jcb = (JCheckBox)cc.component;
                    runtimeCall("jcallback_config_set_boolean", new int[] {cc.configItemPointer, jcb.isSelected()?1:0});
                    break;
                case C_CHOICES:
                    JComboBox jcm = (JComboBox)cc.component;
                    runtimeCall("jcallback_config_set_choice", new int[] {cc.configItemPointer, jcm.getSelectedIndex()});
                    break;
                }
            }
            runtimeCall("jcallback_config_ok", new int[0]);
        }
    }
}
