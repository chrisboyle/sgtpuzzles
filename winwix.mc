% # Source code for the Puzzles installer.
% #
% # The installer is built using WiX, but this file itself is not valid
% # XML input to WiX's candle.exe; instead, this is a template intended
% # to be processed through the standalone 'mason.pl' script provided
% # with Perl's Mason module.

<%class>
has 'version' => (required => 1);
has 'descfile' => (required => 1);
</%class>

<?xml version="1.0" encoding="utf-8"?>

<?if $(var.Win64) = yes ?>
  <?define PlatformProgramFilesFolder = "ProgramFiles64Folder" ?>
<?else ?>
  <?define PlatformProgramFilesFolder = "ProgramFilesFolder" ?>
<?endif ?>

<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">

% # Product tag. The Id component is set to "*", which causes WiX to
% # make up a new GUID every time it's run, whereas UpgradeCode is set
% # to a fixed GUID. This combination allows Windows to
% # recognise each new installer as different (because of Id)
% # versions of the same underlying thing (because of the common
% # UpgradeCode).
  <Product
      Name="Simon Tatham's Portable Puzzle Collection"
      Manufacturer="Simon Tatham"
      Id="*"
      UpgradeCode="<% invent_guid('upgradecode') %>"
      Language="1033" Codepage="1252" Version="<% $winver %>">

% # We force the install scope to perMachine, largely because I
% # don't really understand how to make it usefully switchable
% # between the two. If anyone is a WiX expert and does want to
% # install Puzzles locally in a user account, I hope they'll send a
% # well explained patch!
    <Package Id="*" Keywords="Installer"
             Description="Simon Tatham's Portable Puzzle Collection installer, version <% $.version %>"
             Manufacturer="Simon Tatham"
             InstallerVersion="100" Languages="1033"
             Compressed="yes" SummaryCodepage="1252"
             InstallScope="perMachine" />

% # Permit installing an arbitrary one of these installers
% # over the top of an existing one, whether it's an upgrade or a
% # downgrade.
% # 
% # Setting the REINSTALLMODE property to "amus" (from its default
% # of "omus") forces every component replaced by a different
% # version of the installer to be _actually_ reinstalled; the 'o'
% # flag in the default setting breaks the downgrade case by
% # causing Windows to disallow installation of an older version
% # over the top of a newer one - and to do so _silently_, so the
% # installer claims to have worked fine but the files that would have
% # been downgraded aren't there.
    <MajorUpgrade AllowDowngrades="yes" MigrateFeatures="yes" />
    <Property Id="REINSTALLMODE" Value="amus"/>

% # Boilerplate
    <Media Id="1" Cabinet="puzzles.cab" EmbedCab="yes" />

% # The actual directory structure and list of 'components'
% # (individual files or shortcuts or additions to PATH) that are
% # installed.
    <Directory Id="TARGETDIR" Name="SourceDir">
      <Directory Id="$(var.PlatformProgramFilesFolder)" Name="PFiles">
        <Directory Id="INSTALLDIR" Name="Simon Tatham's Portable Puzzle Collection">

% # The following components all install things in the main
% # install directory (implicitly, by being nested where
% # they are in the XML structure). Most of them also put a
% # shortcut in a subdir of the Start menu, though some of
% # the more obscure things like LICENCE are just there for
% # the sake of being _somewhere_ and don't rate a shortcut.

<%method file_component ($prefix, $filename, $shortcutname)>
% my $filename_id = file_component_name($filename);
<Component Id="File_Component_<% $filename_id %>"
    Guid="<% invent_guid('file:' . $filename) %>">
  <File Id="File_<% $filename_id %>"
        Source="<% $prefix %><% $filename %>" KeyPath="yes">
% if (defined $shortcutname) {
    <Shortcut Id="startmenu_<% $filename_id %>"
              Directory="ProgramMenuDir" WorkingDirectory="INSTALLDIR"
              Name="<% $shortcutname %>" Advertise="no" />
% }
  </File>
</Component>
</%method>

% for my $exe (@exes) {
<% $.file_component('$(var.Bindir)', $exe, $names{$exe}) %>
% }

<% $.file_component('', "puzzles.chm", "Puzzles Manual") %>
<% $.file_component('', "website.url", "Puzzles Web Site") %>
<% $.file_component('', "LICENCE") %>

        </Directory>
      </Directory>

% # This component doesn't actually install anything, but it
% # arranges for the Start Menu _directory_ to be removed again
% # on uninstall. All the actual shortcuts inside the directory
% # are placed by code above here.
      <Directory Id="ProgramMenuFolder" Name="Programs">
        <Directory Id="ProgramMenuDir" Name="Simon Tatham's Portable Puzzle Collection">
          <Component Id="ProgramMenuDir"
                     Guid="<% invent_guid('programmenudir') %>">
            <RemoveFolder Id="ProgramMenuDir" On="uninstall" />
            <RegistryValue Root="HKLM"
                           Key="Software\SimonTatham\Puzzles\StartMenu"
                           Type="string" Value="" KeyPath="yes" />
          </Component>
        </Directory>
      </Directory>
    </Directory>

% # Detect an installation of Puzzles made by the old Inno Setup
% # installer, and refuse to run if we find one. I don't know what
% # would happen if you tried anyway, but since they install files
% # at the same pathnames, it surely wouldn't end well.
% # 
% # It could be argued that a better approach would be to actually
% # _launch_ the Inno Setup uninstaller automatically at this
% # point (prompting the user first, of course), but I'm not
% # nearly skilled enough with WiX to know how, or even if it's
% # feasible.
    <Property Id="LEGACYINNOSETUPINSTALLERNATIVE32PROPERTY">
      <RegistrySearch
          Id="LegacyInnoSetupInstallerNative32RegSearch"
          Root="HKLM"
          Key="SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Simon Tatham's Portable Puzzle Collection_is1"
          Name="QuietUninstallString" Type="raw" />
    </Property>
    <Property Id="LEGACYINNOSETUPINSTALLER32ON64PROPERTY">
      <RegistrySearch
          Id="LegacyInnoSetupInstaller32On64RegSearch"
          Root="HKLM"
          Key="SOFTWARE\Wow6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Simon Tatham's Portable Puzzle Collection_is1"
          Name="QuietUninstallString" Type="raw" />
    </Property>
    <Condition Message="A version of this software is already installed on this system using its old Inno Setup installer. Please uninstall that before running the new installer.">
      <![CDATA[Installed OR
               (LEGACYINNOSETUPINSTALLERNATIVE32PROPERTY = "" AND
                LEGACYINNOSETUPINSTALLER32ON64PROPERTY = "")]]>
    </Condition>

% # Separate the installation into 'features', which are parts of
% # the install that can be chosen separately. Trivial: there is only
% # one feature here.
    <Feature Id="FilesFeature" Level="1" Absent="disallow" AllowAdvertise="no"
             Title="Install puzzle games">
% for my $exe (@exes) {
      <ComponentRef Id="File_Component_<% file_component_name($exe) %>" />
% }
      <ComponentRef Id="File_Component_<% file_component_name("puzzles.chm") %>" />
      <ComponentRef Id="File_Component_<% file_component_name("website.url") %>" />
      <ComponentRef Id="File_Component_<% file_component_name("LICENCE") %>" />
      <ComponentRef Id="ProgramMenuDir" />
    </Feature>

% # Installer user interface.
% # 
% # Basically like WixUI_InstallDir, only I've ripped out the EULA.
    <UIRef Id="WixUI_Common" />

    <UI>
      <TextStyle Id="WixUI_Font_Normal" FaceName="Tahoma" Size="8" />
      <TextStyle Id="WixUI_Font_Bigger" FaceName="Tahoma" Size="12" />
      <TextStyle Id="WixUI_Font_Title" FaceName="Tahoma" Size="9" Bold="yes" />

      <Property Id="DefaultUIFont" Value="WixUI_Font_Normal" />
      <Property Id="WixUI_Mode" Value="InstallDir" />

      <DialogRef Id="BrowseDlg" />
      <DialogRef Id="DiskCostDlg" />
      <DialogRef Id="ErrorDlg" />
      <DialogRef Id="FatalError" />
      <DialogRef Id="FilesInUse" />
      <DialogRef Id="MsiRMFilesInUse" />
      <DialogRef Id="PrepareDlg" />
      <DialogRef Id="ProgressDlg" />
      <DialogRef Id="ResumeDlg" />
      <DialogRef Id="UserExit" />

      <Publish Dialog="BrowseDlg" Control="OK" Event="DoAction" Value="WixUIValidatePath" Order="3">1</Publish>
      <Publish Dialog="BrowseDlg" Control="OK" Event="SpawnDialog" Value="InvalidDirDlg" Order="4"><![CDATA[NOT WIXUI_DONTVALIDATEPATH AND WIXUI_INSTALLDIR_VALID<>"1"]]></Publish>

      <Publish Dialog="ExitDialog" Control="Finish" Event="EndDialog" Value="Return" Order="999">1</Publish>

      <Publish Dialog="WelcomeDlg" Control="Next" Event="NewDialog" Value="InstallDirDlg">NOT Installed</Publish>
      <Publish Dialog="WelcomeDlg" Control="Next" Event="NewDialog" Value="VerifyReadyDlg">Installed</Publish>

      <Publish Dialog="InstallDirDlg" Control="Back" Event="NewDialog" Value="WelcomeDlg">1</Publish>
      <Publish Dialog="InstallDirDlg" Control="Next" Event="SetTargetPath" Value="[WIXUI_INSTALLDIR]" Order="1">1</Publish>
      <Publish Dialog="InstallDirDlg" Control="Next" Event="DoAction" Value="WixUIValidatePath" Order="2">NOT WIXUI_DONTVALIDATEPATH</Publish>
      <Publish Dialog="InstallDirDlg" Control="Next" Event="SpawnDialog" Value="InvalidDirDlg" Order="3"><![CDATA[NOT WIXUI_DONTVALIDATEPATH AND WIXUI_INSTALLDIR_VALID<>"1"]]></Publish>
      <Publish Dialog="InstallDirDlg" Control="Next" Event="NewDialog" Value="VerifyReadyDlg" Order="4">WIXUI_DONTVALIDATEPATH OR WIXUI_INSTALLDIR_VALID="1"</Publish>
      <Publish Dialog="InstallDirDlg" Control="ChangeFolder" Property="_BrowseProperty" Value="[WIXUI_INSTALLDIR]" Order="1">1</Publish>
      <Publish Dialog="InstallDirDlg" Control="ChangeFolder" Event="SpawnDialog" Value="BrowseDlg" Order="2">1</Publish>

      <Publish Dialog="VerifyReadyDlg" Control="Back" Event="NewDialog" Value="InstallDirDlg" Order="1">NOT Installed</Publish>
      <Publish Dialog="VerifyReadyDlg" Control="Back" Event="NewDialog" Value="MaintenanceTypeDlg" Order="2">Installed AND NOT PATCH</Publish>
      <Publish Dialog="VerifyReadyDlg" Control="Back" Event="NewDialog" Value="WelcomeDlg" Order="2">Installed AND PATCH</Publish>

      <Publish Dialog="MaintenanceWelcomeDlg" Control="Next" Event="NewDialog" Value="MaintenanceTypeDlg">1</Publish>

      <Publish Dialog="MaintenanceTypeDlg" Control="RepairButton" Event="NewDialog" Value="VerifyReadyDlg">1</Publish>
      <Publish Dialog="MaintenanceTypeDlg" Control="RemoveButton" Event="NewDialog" Value="VerifyReadyDlg">1</Publish>
      <Publish Dialog="MaintenanceTypeDlg" Control="Back" Event="NewDialog" Value="MaintenanceWelcomeDlg">1</Publish>

% # This ARPNOMODIFY flag prohibits changing the set of
% # installed features, because we don't have any.
      <Property Id="ARPNOMODIFY" Value="1" />
    </UI>

% # Glue: tell the install dir part of the UI what id my actual
% # install dir is known by. Otherwise the former won't know how
% # to alter the setting of the latter.
    <Property Id="WIXUI_INSTALLDIR" Value="INSTALLDIR" />

% # Include my custom installer artwork, created in Buildscr. FIXME:
% # create some!
% #    <WixVariable Id="WixUIDialogBmp" Value="msidialog.bmp" />
% #    <WixVariable Id="WixUIBannerBmp" Value="msibanner.bmp" />

  </Product>
</Wix>

<%init>

use Digest::SHA qw(sha512_hex);
use Time::Local;

die "bad date format" if $.version !~ /^(\d{4})(\d{2})(\d{2})/;
my $date = timegm(0,0,0,$3,$2-1,$1);
my $integer_date = int($date / 86400) - 6000;
my $winver = sprintf "0.0.%d.0", $integer_date;

my @exes;
my %names;
{
    open my $descfh, "<", $.descfile or die "$.descfile: open: $!\n";
    while (<$descfh>) {
        chomp;
        my @fields = split /:/;
        push @exes, $fields[1];
        $names{$fields[1]} = $fields[2];
    }
    close $descfh;
}

sub file_component_name($) {
    my ($id) = @_;
    $id =~ s!.*\\!!;
    $id =~ y!A-Za-z0-9!_!cs;
    return $id;
}

sub invent_guid($) {
    my ($name) = @_;

    # Invent a GUID. We'll need a lot of these in this installer - one
    # per puzzle game and a few standard ones - and we'd like to
    # arrange for them to be stable between versions of the installer,
    # while not having to _manually_ invent one every time.
    #
    # So we generate our GUIDs by hashing a pile of fixed (but
    # originally randomly generated) data with an identifying string
    # that acts as source for the particular GUID. For example,
    # hashing (fixed_random_data || "upgradecode") produces the GUID
    # used as the upgrade code, and (fixed_random_data ||
    # "installfile:" || filename) gives the GUID for an MSI component
    # that installs a specific file.
    #
    # Hashing _just_ the id strings would clearly be cheating (it's
    # quite conceivable that someone might hash the same string for
    # another reason and so generate a colliding GUID), but hashing a
    # whole SHA-512 data block of random gibberish as well should make
    # these GUIDs pseudo-random enough to not collide with anyone
    # else's.

    my $randdata = pack "N*",
    0xCCAA8D31,0x42931BD9,0xA9D9878A,0x72E4FB9C,0xEA9B11DE,0x4FF17AEC,
    0x1AFA2DEC,0xB662640A,0x780143F5,0xBFFFF0FC,0x01CB46CF,0x832AE842,
    0xBCCDDA12,0x4DB24889,0xEC7A9BCD,0xBCCF70ED,0x85800659,0x8ABA9524,
    0xE484F8D6,0x5CBE55B3,0x95AD9B3D,0x0555F432,0x46737F89,0xE981471C,
    0x4B3419AD,0xD4E49DF4,0xB3EF69DE,0x2A7E391E,0xF3C3D370,0x3EA5E9FC,
    0xB35A57ED,0x52B21099,0x9BD99092,0x7B5097AE,0x4DBE59BD,0x2FCC709B,
    0xC555A779,0x4AE2E5AB,0xB7C74314,0x7F9194CF,0x8FFBCA88,0x46263306,
    0x4C714DF7,0x07FE8CEE,0x28974885,0x0869865D,0xBB5B0EA4,0x4064A928,
    0x28C41910,0x07030758,0x19E66319,0x050C9D4E,0xD79A37FB,0xF232D98B,
    0x0C3E4C25,0xC94F865B,0xB6D86BED,0x87DB718D,0xC65D4C43,0x7C8BBF6A,
    0x9DFDD26A,0x8C478976,0x53E47640,0x263E04AA,0xDD7C5456,0x766BDF50,
    0x86946E34,0xE80D8DE3,0xFB92949E,0x691FDAD0,0x96AB210D,0xB278D60B,
    0x71C7DC6B,0xD49846FC,0x38953F66,0x39777D72,0x4A0F80E5,0xFE1DD1A4,
    0xDA9D191A,0xA32844AD,0x171BFBCC,0xA7D556F6,0xF9F6D944,0xF9687060,
    0xDDDB81D0,0xE9AF4F2F,0xEF84685A,0x8A172492,0x50615EFC,0x20817881,
    0xC3E507E5,0x33189771,0xB9E2EBBD,0x2AAE34A3,0x8D3E7486,0x0A448F13,
    0x94F92A81,0x5150A34F,0x5ED50563,0xAD801A42,0xD0617AFA,0xB78F85F7,
    0x0019D384,0xF0F1C06F,0x6EF8D5B3,0x38092D09,0xC87CD4B3,0xE9D8C84F,
    0xE036648C,0xF2500BD9,0xCF069B5C,0x835326BA,0xCD107B6A,0x64F152B3,
    0xA9663E24,0x33ED5E08,0xC3B24F7E,0xA83205C8,0xA0560F30,0xDFF1226E,
    0xF1A404B7,0x9C2B4EF2,0x62802F88,0xE393A05F,0xC7F72F7B,0x1CD989BD,
    0x725AB516,0xA84F7E39,0xACC3572A,0x820ACB2D,0xAFF5BF06,0x476A2405,
    0x90953482,0x8E8035E1,0x1FB95F6E,0x01FD6766,0x1E63D78E,0xD7D42220,
    0x188D23E4,0x1941BCC5,0xEE1E6487,0x6E197F82,0x32772265,0x9B79D0C8,
    0xB4B617A1,0xCD2475B4,0xDE0F410B,0xE9CF69E4,0x831AC9A4,0xD549A00E,
    0x12ECC561,0x3D636A43,0x1FFFC99A,0xF79401C5,0xAA1D8251,0x84D29609,
    0x5464CB71,0xB28AAE5A,0x4AD934FC,0x347E8A5D,0xC87BCBA0,0x67172E33,
    0xEC70E245,0x4289A9EF,0xA8AF6BA5,0x1528FE0C,0xA87CBFF8,0x79AE1554,
    0xBD59DB9E,0xF1879F94,0x14D7E9F6,0x85196447,0xC4363A67,0x7E02A325,
    0x54051E05,0xABAFE646,0x65D5DF96,0xD3F8173B,0x09D475E7,0x9BF7BD0C,
    0x2DAF371A,0x793D063C,0xA68FD47B,0xBE2500A7,0x0D5071C4,0x08384AC8,
    0xF6CFE74E,0x124A5086,0x03475917,0x47267765,0x56F7DF31,0xE5696381,
    0xEB2B4668,0x78345B5B,0x6E2AFC0F,0x3AD0D43B,0x5C3C2BC9,0x833AB4A0,
    0x1DE2CDBF,0x4DDDCF58,0xEA25D69B,0x36E9B3B0,0xC8B11465,0x066A997E,
    0x9D51C7CD,0x8C6AE686,0xAFB06CE6,0xCC3F3017,0x6E4E4CC0,0x85A34875,
    0x498FE759,0xC24B6332,0xEBCD2257,0xE70FC659,0x439EC788,0xB47F2A06,
    0x696EE8A7,0xF70A31B8,0xECD840F7,0x80AE5E7A,0xC6EDF8AE,0x8165EAFD,
    0x5DAE5ADE,0x9FFD39CE,0xFC6B4C23,0x02BCA024,0xC1497A68,0xD18153EF,
    0xD787EA51,0x91386720,0xBF6E2200,0x98638391,0x144B9148,0x9A554DE1,
    0xA940DC7F,0x37C08265,0x7B782C60,0xC081FDD7,0x62B47201,0x43427563,
    0x1B66E983,0x3DAC0305,0x21E9DEA8,0xA9490EE0,0xE2EFD37D,0x3501F306,
    0x16361BD5,0x668B219D,0x17177844,0x3861A9A4,0x222403B2,0xB29F6714,
    0x7A2A938A,0xBC797418,0x3B241624,0x9163C3F2;
    my $digest = sha512_hex($name . "\0" . $randdata);
    return sprintf("%s-%s-%04x-%04x-%s",
                   substr($digest,0,8),
                   substr($digest,8,4),
                   0x4000 | (0xFFF & hex(substr($digest,12,4))),
                   0x8000 | (0x3FFF & hex(substr($digest,16,4))),
                   substr($digest,20,12));
}
</%init>
