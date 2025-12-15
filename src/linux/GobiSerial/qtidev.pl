#!/usr/bin/perl
use Sys::Syslog;
use Sys::Syslog qw(:DEFAULT setlogsock);  # default set, plus setlogsock

my ($inf_file, $tty_dev) = @ARGV;
my ($myerr);
openlog("", 'noeol,nonul', LOG_LOCAL0) or print "failed to open syslog\n";
#syslog('debug', "inf=$inf_file tty=$tty_dev");
if ($inf_file eq "") { die "Usage: qtidev.pl <inf_file>\n"; }

$/ = undef;
open FILE, "<$inf_file"  or $myerr = 1;
if ($myerr == 1)
{
   syslog('debug', "failed to open file: $inf_file\n");
   die "failed to open file: $inf_file\n";
}
my $inf_text = <FILE>;
close FILE;

if ($tty_dev eq "")
{
   opendir(DIR, "/dev") or die "Failed to open dev directory\n";
   while (my $tty_file = readdir(DIR)) 
   {
      if ($tty_file =~ /^ttyUSB/ig)
      {
         CreateSymbolicName($tty_file, $inf_text, 0);
      }
   }
   closedir(DIR);
}
else
{
   CreateSymbolicName($tty_dev, $inf_text, 1);
}
closelog();

sub CreateSymbolicName
{
   my ($in_tty, $inf_text, $tty_from_usr) = @_;
   my ($vid, $pid, $ifn, $dev_name, $serialNum);

   my $udevOut = qx(udevadm info --attribute-walk --name /dev/$in_tty);
   if ($udevOut eq "") { return; }

   #($ifn, $vid, $pid) = $udevOut =~ /bInterfaceNumber\}==\"(.{2}).+?idVendor\}==\"(.{4}).+?idProduct\}==\"(.{4})/is;
   ($ifn) = $udevOut =~ /bInterfaceNumber\}==\"(.{2})/is;
   ($vid) = $udevOut =~ /idVendor\}==\"(.{4})/is;
   ($pid) = $udevOut =~ /idProduct\}==\"(.{4})/is;
   if (($ifn eq "") || ($vid eq "") || ($pid eq "")) { return; }
   ($serialNum) = $udevOut =~ /ATTRS\{serial\}==\"(.*?)\"/is;
   my $ifn_v = eval($ifn);

   my $matching_str = "QTIDevice$pid$ifn_v";
   ($dev_name) = $inf_text =~ /$matching_str.*?=.*?\"(.+?)\"/ig;
   if ($dev_name ne "")  # found a matching device name
   {
      $dev_name2 = $dev_name . ":$serialNum:$in_tty";
      $dev_name2 =~ s/ /_/g;
      unlink($dev_name2);
      syslog('debug', $dev_name2);
      print $dev_name2;
      if ($tty_from_usr == 0)  # create symbolic link only with internally scanned tty names
      {
         #$dev_name = "\"".$dev_name . " ($in_tty)" . "\"";
         my $sbl_cmd = "ln -s /dev/$in_tty ./$dev_name2";
         system($sbl_cmd);
      }
   }
}
