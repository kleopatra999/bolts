/*****************************************************************************
           BOLTS version 1.0 - Nathan D Richards, December 1996
		Updated December 1997 to version 1.2
                   Updated May 2001 to version 1.3
  	      Updated June 2001 to version 2 - BUG FREE

        Based on NUTS 3.3.3 by Neil Robertson, credits below.

Email: nathanr@nathanr.com
WWW:   http://www.nathanr.com

AOL & Yahoo Messengers: NathanR21
ICQ:   52687441
MSN Messenger: nathanr23@hotmail.com
*****************************************************************************/

/*****************************************************************************
    NUTS version 3.3.3 (Triple Three :) - Copyright (C) Neil Robertson 1996
                     Last update: 18th November 1996
                          (Bug fixed edition)

 This software is provided as is. It is not intended as any sort of bullet
 proof system for commercial operation (though you may use it to set up a 
 pay-to-use system, just don't attempt to sell the code itself) and I accept 
 no liability for any problems that may arise from you using it. Since this is 
 freeware (NOT public domain , I have not relinquished the copyright) you may 
 distribute it as you see fit and you may alter the code to suit your needs.

 Read the COPYRIGHT file for further information.

 Neil Robertson. 

 Email    : neil@ogham.demon.co.uk
 Home page: http://ogham.demon.co.uk/neil.html (need JavaScript enabled browser)
 NUTS page: http://ogham.demon.co.uk/nuts.html
 Newsgroup: alt.talkers.nuts

 NB: This program listing looks best when the tab length is 5 chars which is
 "set ts=5" in vi.

 *****************************************************************************/

#include <stdio.h>
#ifdef _AIX
#include <sys/select.h>
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <setjmp.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>

#include "bolts.h"
#include "hostinfo.h"
#include "quotes.h"
#include "blackjack.h"
#include "blackjack.c"

#define VERSION "3.3.3"

/*** This function calls all the setup routines and also contains the
	main program loop ***/
main(argc,argv)
int argc;
char *argv[];
{
fd_set readmask; 
int i,len; 
char inpstr[ARR_SIZE];
char *remove_first();
UR_OBJECT user,next;
NL_OBJECT nl;

strcpy(progname,argv[0]);
if (argc<2) strcpy(confile,CONFIGFILE);
else strcpy(confile,argv[1]);

/* Startup */
printf("\n*** Bolts %s server booting ***\n\n",boltsversion);
init_globals();
write_syslog("\n*** SERVER BOOTING ***\n",0);
set_date_time();
init_signals();
load_and_parse_config();
init_sockets();
if (auto_connect) init_connections();
else printf("Skipping connect stage.\n");
check_messages(NULL,1);

/* Run in background automatically. */
switch(fork()) {
	case -1: boot_exit(11);  /* fork failure */
	case  0: break; /* child continues */
	default: sleep(1); exit(0);  /* parent dies */
	}
if (argc>2) if (atoi(argv[2])>1) do_srload();
reset_alarm();
printf("\n*** Booted with PID %d ***\n\n",getpid());
sprintf(text,"*** Booted successfully with PID %d %s ***\n\n",getpid(),long_date(1));
write_syslog(text,0);

/**** Main program loop. *****/
setjmp(jmpvar); /* jump to here if we crash and crash_action = IGNORE */
while(1) {
	/* set up mask then wait */
	setup_readmask(&readmask);
	if (select(FD_SETSIZE,&readmask,0,0,0)==-1) continue;

	/* check for connection to listen sockets */
	for(i=0;i<3;++i) {
		if (FD_ISSET(listen_sock[i],&readmask)) 
			accept_connection(listen_sock[i],i);
		}

	/* Cycle through client-server connections to other talkers */
	for(nl=nl_first;nl!=NULL;nl=nl->next) {
		no_prompt=0;
		if (nl->type==UNCONNECTED || !FD_ISSET(nl->socket,&readmask)) 
			continue;
		/* See if remote site has disconnected */
		if (!(len=read(nl->socket,inpstr,sizeof(inpstr)-3))) {
			if (nl->stage==UP)
				sprintf(text,"NETLINK: Remote disconnect by %s.\n",nl->service);
			else sprintf(text,"NETLINK: Remote disconnect by site %s.\n",nl->site);
			write_syslog(text,1);
			sprintf(text,"[ ~OLSYSTEM:~RS Lost link to %s in the %s. ]\n",nl->service,nl->connect_room->name);
			write_room(NULL,text);
			shutdown_netlink(nl);
			continue;
			}
		inpstr[len]='\0'; 
		exec_netcom(nl,inpstr);
		}

	/* Cycle through users. Use a while loop instead of a for because
	    user structure may be destructed during loop in which case we
	    may lose the user->next link. */
	user=user_first;
	while(user!=NULL) {
		next=user->next; /* store in case user object is destructed */
		/* If remote user or clone ignore */
		if (user->type!=USER_TYPE) {  user=next;  continue; }

		/* see if any data on socket else continue */
		if (!FD_ISSET(user->socket,&readmask)) { user=next;  continue; }
	
		/* see if client (eg telnet) has closed socket */
		inpstr[0]='\0';
		if (!(len=read(user->socket,inpstr,sizeof(inpstr)))) {
			disconnect_user(user);  user=next;
			continue;
			}
		/* ignore control code replies */
		if ((unsigned char)inpstr[0]==255) { user=next;  continue; }

		/* Deal with input chars. If the following if test succeeds we
		   are dealing with a character mode client so call function. */
		if (inpstr[len-1]>=32 || user->buffpos) {
			if (get_charclient_line(user,inpstr,len)) goto GOT_LINE;
			user=next;  continue;
			}
		else terminate(inpstr);

		GOT_LINE:
		no_prompt=0;  
		com_num=-1;
		force_listen=0; 
		destructed=0;
		user->buff[0]='\0';  
		user->buffpos=0;
		user->last_input=time(0);
		if (user->login) {
			login(user,inpstr);  user=next;  continue;  
			}

		/* If a dot on its own then execute last inpstr unless its a misc
		   op or the user is on a remote site */
		if (!user->misc_op) {
			if (!strcmp(inpstr,".") && user->inpstr_old[0]) {
				strcpy(inpstr,user->inpstr_old);
				sprintf(text,"%s\n",inpstr);
				write_user(user,text);
				}
			/* else save current one for next time */
			else {
				if (inpstr[0]) strncpy(user->inpstr_old,inpstr,REVIEW_LEN);
				}
			}

		/* Main input check */
		clear_words();
		word_count=wordfind(inpstr);
		if (user->afk) {
			if (user->afk==2) {
				if (!word_count) {  
					if (user->command_mode) prompt(user);
					user=next;  continue;  
					}
				if (strcmp((char *)crypt(word[0],"NU"),user->pass)) {
					write_user(user,"Incorrect password.\n"); 
					prompt(user);  user=next;  continue;
					}
				cls(user);
				write_user(user,"Session unlocked, you are no longer AFK.\n");
				}	
			else write_user(user,"You are no longer AFK.\n");  
			user->afk_mesg[0]='\0';
			if (user->vis) {
				sprintf(text,"%s has RETURNED!\n",user->name);
				write_room_except(user->room,text,user);
				}
			if (user->afk==2) {
				user->afk=0;  prompt(user);  user=next;  continue;
				}
			user->afk=0;
			}
		if (!word_count) {
			if (misc_ops(user,inpstr))  {  user=next;  continue;  }
			if (user->room==NULL) {
				sprintf(text,"ACT %s NL\n",user->name);
				write_sock(user->netlink->socket,text);
				}
			if (user->command_mode) prompt(user);
			user=next;  continue;
			}
		if (misc_ops(user,inpstr))  {  user=next;  continue;  }
		com_num=-1;
		if (user->command_mode || strchr(".;!<>-#@'",inpstr[0])) 
			exec_com(user,inpstr);
		else say(user,inpstr);
		if (!destructed) {
			if (user->room!=NULL)  prompt(user); 
			else {
				switch(com_num) {
					case -1  : /* Unknown command */
					case HOME:
					case QUIT:
					case MODE:
					case PROMPT: 
					case SUICIDE:
					case REBOOT:
					case SHUTDOWN: prompt(user);
					}
				}
			}
		user=next;
		}
	} /* end while */
}


/************ MAIN LOOP FUNCTIONS ************/

/*** Set up readmask for select ***/
setup_readmask(mask)
fd_set *mask;
{
UR_OBJECT user;
NL_OBJECT nl;
int i;

FD_ZERO(mask);
for(i=0;i<3;++i) FD_SET(listen_sock[i],mask);
/* Do users */
for (user=user_first;user!=NULL;user=user->next) 
	if (user->type==USER_TYPE) FD_SET(user->socket,mask);

/* Do client-server stuff */
for(nl=nl_first;nl!=NULL;nl=nl->next) 
	if (nl->type!=UNCONNECTED) FD_SET(nl->socket,mask);
}


/*** Accept incoming connections on listen sockets ***/
accept_connection(lsock,num)
int lsock,num;
{
UR_OBJECT user,create_user();
NL_OBJECT create_netlink();
char *get_ip_address(),site[80];
struct sockaddr_in acc_addr;
int accept_sock,size;

size=sizeof(struct sockaddr_in);
accept_sock=accept(lsock,(struct sockaddr *)&acc_addr,&size);
if (num==2) {
	accept_server_connection(accept_sock,acc_addr);  return;
	}
strcpy(site,get_ip_address(acc_addr));
if (site_banned(site)) {
	write_sock(accept_sock,"\n\rLogins from your site/domain are banned.\n\n\r");
	close(accept_sock);
	sprintf(text,"Attempted login from banned site %s.\n",site);
	write_syslog(text,1);
	sprintf(text,"[ ~OLAttempted login from banned site %s~RS ]\n",site);
	write_level(ARCH,1,text,NULL);
	return;
	}
more(NULL,accept_sock,MOTD1); /* send pre-login message */
if (num_of_users+num_of_logins>=max_users && !num) {
	write_sock(accept_sock,"\n\rSorry, the talker is full at the moment.\n\n\r");
	close(accept_sock);  
	return;
	}
if ((user=create_user())==NULL) {
	sprintf(text,"\n\r%s: unable to create session.\n\n\r",syserror);
	write_sock(accept_sock,text);
	close(accept_sock);  
	return;
	}
user->socket=accept_sock;
user->login=3;
user->last_input=time(0);
if (!num) user->port=port[0]; 
else {
	user->port=port[1];
	write_user(user,"** Wizport login **\n\n");
	}
strcpy(user->site,site);
user->site_port=(int)ntohs(acc_addr.sin_port);
echo_on(user);
write_user(user,"Enter a name: ");
num_of_logins++;
sprintf(text,"[ ~OLPRELOGIN: ~FR%s~RS ]\n",user->site);
write_level(WIZARD,1,text,NULL);
}


/*** Get net address of accepted connection ***/
char *get_ip_address(acc_addr)
struct sockaddr_in acc_addr;
{
static char site[80];
struct hostent *host;

strcpy(site,(char *)inet_ntoa(acc_addr.sin_addr)); /* get number addr */
if ((host=gethostbyaddr((char *)&acc_addr.sin_addr,4,AF_INET))!=NULL)
	strcpy(site,host->h_name); /* copy name addr. */
strtolower(site);
return site;
}


/*** See if users site is banned ***/
site_banned(site)
char *site;
{
FILE *fp;
char line[82],filename[80];

if (!strcmp(site,"nathanr.com") || !strcmp(site,"205.245.53.18") ||
!strcmp(site,"orbit.zepa.net") || !strcmp(site,"205.245.53.14")) 
	return 0;
sprintf(filename,"%s/%s",DATAFILES,SITEBAN);
if (!(fp=fopen(filename,"r"))) return 0;
fscanf(fp,"%s",line);
while(!feof(fp)) {
	if (strstr(site,line)) {  fclose(fp);  return 1;  }
	fscanf(fp,"%s",line);
	}
fclose(fp);
return 0;
}


/*** See if user is banned ***/
user_banned(name)
char *name;
{
FILE *fp;
char line[82],filename[80];

sprintf(filename,"%s/%s",DATAFILES,USERBAN);
if (!(fp=fopen(filename,"r"))) return 0;
fscanf(fp,"%s",line);
while(!feof(fp)) {
	if (!strcmp(line,name)) {  fclose(fp);  return 1;  }
	fscanf(fp,"%s",line);
	}
fclose(fp);
return 0;
}


/*** Attempt to get '\n' terminated line of input from a character
     mode client else store data read so far in user buffer. ***/
get_charclient_line(user,inpstr,len)
UR_OBJECT user;
char *inpstr;
int len;
{
int l;

for(l=0;l<len;++l) {
	/* see if delete entered */
	if (inpstr[l]==8 || inpstr[l]==127) {
		if (user->buffpos) {
			user->buffpos--;
			if (user->charmode_echo) write_user(user,"\b \b");
			}
		continue;
		}
	user->buff[user->buffpos]=inpstr[l];
	/* See if end of line */
	if (inpstr[l]<32 || user->buffpos+2==ARR_SIZE) {
		terminate(user->buff);
		strcpy(inpstr,user->buff);
		if (user->charmode_echo) write_user(user,"\n");
		return 1;
		}
	user->buffpos++;
	}
if (user->charmode_echo
    && ((user->login!=2 && user->login!=1) || password_echo)) 
	write(user->socket,inpstr,len);
return 0;
}


/*** Put string terminate char. at first char < 32 ***/
terminate(str)
char *str;
{
int i;
for (i=0;i<ARR_SIZE;++i)  {
	if (*(str+i)<32) {  *(str+i)=0;  return;  } 
	}
str[i-1]=0;
}


/*** Get words from sentence. This function prevents the words in the 
     sentence from writing off the end of a word array element. This is
     difficult to do with sscanf() hence I use this function instead. ***/
wordfind(inpstr)
char *inpstr;
{
int wn,wpos;

wn=0; wpos=0;
do {
	while(*inpstr<33) if (!*inpstr++) return wn;
	while(*inpstr>32 && wpos<WORD_LEN-1) {
		word[wn][wpos]=*inpstr++;  wpos++;
		}
	word[wn][wpos]='\0';
	wn++;  wpos=0;
	} while (wn<MAX_WORDS);
return wn-1;
}


/*** clear word array etc. ***/
clear_words()
{
int w;
for(w=0;w<MAX_WORDS;++w) word[w][0]='\0';
word_count=0;
}


/************ PARSE CONFIG FILE **************/

load_and_parse_config()
{
FILE *fp;
char line[81]; /* Should be long enough */
char c,filename[80];
int i,section_in,got_init,got_rooms;
RM_OBJECT rm1,rm2;
NL_OBJECT nl;

section_in=0;
got_init=0;
got_rooms=0;

sprintf(filename,"%s/%s",DATAFILES,confile);
printf("Parsing config file \"%s\"...\n",filename);
if (!(fp=fopen(filename,"r"))) {
	perror("NUTS: Can't open config file");  boot_exit(1);
	}
/* Main reading loop */
config_line=0;
fgets(line,81,fp);
while(!feof(fp)) {
	config_line++;
	for(i=0;i<8;++i) wrd[i][0]='\0';
	sscanf(line,"%s %s %s %s %s %s %s %s",wrd[0],wrd[1],wrd[2],wrd[3],wrd[4],wrd[5],wrd[6],wrd[7]);
	if (wrd[0][0]=='#' || wrd[0][0]=='\0') {
		fgets(line,81,fp);  continue;
		}
	/* See if new section */
	if (wrd[0][strlen(wrd[0])-1]==':') {
		if (!strcmp(wrd[0],"INIT:")) section_in=1;
		else if (!strcmp(wrd[0],"ROOMS:")) section_in=2;
			else if (!strcmp(wrd[0],"SITES:")) section_in=3; 
				else {
					fprintf(stderr,"NUTS: Unknown section header on line %d.\n",config_line);
					fclose(fp);  boot_exit(1);
					}
		}
	switch(section_in) {
		case 1: parse_init_section();  got_init=1;  break;
		case 2: parse_rooms_section(); got_rooms=1; break;
		case 3: parse_sites_section(); break;
		default:
			fprintf(stderr,"NUTS: Section header expected on line %d.\n",config_line);
			boot_exit(1);
		}
	fgets(line,81,fp);
	}
fclose(fp);

/* See if required sections were present (SITES is optional) and if
   required parameters were set. */
if (!got_init) {
	fprintf(stderr,"NUTS: INIT section missing from config file.\n");
	boot_exit(1);
	}
if (!got_rooms) {
	fprintf(stderr,"NUTS: ROOMS section missing from config file.\n");
	boot_exit(1);
	}
if (!verification[0]) {
	fprintf(stderr,"NUTS: Verification not set in config file.\n");
	boot_exit(1);
	}
if (!port[0]) {
	fprintf(stderr,"NUTS: Main port number not set in config file.\n");
	boot_exit(1);
	}
if (!port[1]) {
	fprintf(stderr,"NUTS: Wiz port number not set in config file.\n");
	boot_exit(1);
	}
if (!port[2]) {
	fprintf(stderr,"NUTS: Link port number not set in config file.\n");
	boot_exit(1);
	}
if (port[0]==port[1] || port[1]==port[2] || port[0]==port[2]) {
	fprintf(stderr,"NUTS: Port numbers must be unique.\n");
	boot_exit(1);
	}
if (room_first==NULL) {
	fprintf(stderr,"NUTS: No rooms configured in config file.\n");
	boot_exit(1);
	}

/* Parsing done, now check data is valid. Check room stuff first. */
for(rm1=room_first;rm1!=NULL;rm1=rm1->next) {
	for(i=0;i<MAX_LINKS;++i) {
		if (!rm1->link_label[i][0]) break;
		for(rm2=room_first;rm2!=NULL;rm2=rm2->next) {
			if (rm1==rm2) continue;
			if (!strcmp(rm1->link_label[i],rm2->label)) {
				rm1->link[i]=rm2;  break;
				}
			}
		if (rm1->link[i]==NULL) {
			fprintf(stderr,"NUTS: Room %s has undefined link label '%s'.\n",rm1->name,rm1->link_label[i]);
			boot_exit(1);
			}
		}
	}

/* Check external links */
for(rm1=room_first;rm1!=NULL;rm1=rm1->next) {
	for(nl=nl_first;nl!=NULL;nl=nl->next) {
		if (!strcmp(nl->service,rm1->name)) {
			fprintf(stderr,"NUTS: Service name %s is also the name of a room.\n",nl->service);
			boot_exit(1);
			}
		if (rm1->netlink_name[0] 
		    && !strcmp(rm1->netlink_name,nl->service)) {
			rm1->netlink=nl;  break;
			}
		}
	if (rm1->netlink_name[0] && rm1->netlink==NULL) {
		fprintf(stderr,"NUTS: Service name %s not defined for room %s.\n",rm1->netlink_name,rm1->name);
		boot_exit(1);
		}
	}

/* Load room descriptions */
for(rm1=room_first;rm1!=NULL;rm1=rm1->next) {
	sprintf(filename,"%s/%s.R",DATAFILES,rm1->name);
	if (!(fp=fopen(filename,"r"))) {
		fprintf(stderr,"NUTS: Can't open description file for room %s.\n",rm1->name);
		sprintf(text,"ERROR: Couldn't open description file for room %s.\n",rm1->name);
		write_syslog(text,0);
		continue;
		}
	i=0;
	c=getc(fp);
	while(!feof(fp)) {
		if (i==ROOM_DESC_LEN) {
			fprintf(stderr,"NUTS: Description too long for room %s.\n",rm1->name);
			sprintf(text,"ERROR: Description too long for room %s.\n",rm1->name);
			write_syslog(text,0);
			break;
			}
		rm1->desc[i]=c;  
		c=getc(fp);  ++i;
		}
	rm1->desc[i]='\0';
	fclose(fp);
	}
}



/*** Parse init section ***/
parse_init_section()
{
static int in_section=0;
int op,val;
char *options[]={ 
"mainport","wizport","linkport","system_logging","minlogin_level","mesg_life",
"wizport_level","prompt_def","gatecrash_level","min_private","ignore_mp_level",
"rem_user_maxlevel","rem_user_deflevel","verification","mesg_check_time",
"max_users","heartbeat","login_idle_time","user_idle_time","password_echo",
"ignore_sigterm","auto_connect","max_clones","ban_swearing","crash_action",
"colour_def","time_out_afks","allow_caps_in_name","charecho_def",
"time_out_maxlevel","*"
};

if (!strcmp(wrd[0],"INIT:")) { 
	if (++in_section>1) {
		fprintf(stderr,"NUTS: Unexpected INIT section header on line %d.\n",config_line);
		boot_exit(1);
		}
	return;
	}
op=0;
while(strcmp(options[op],wrd[0])) {
	if (options[op][0]=='*') {
		fprintf(stderr,"NUTS: Unknown INIT option on line %d.\n",config_line);
		boot_exit(1);
		}
	++op;
	}
if (!wrd[1][0]) {
	fprintf(stderr,"NUTS: Required parameter missing on line %d.\n",config_line);
	boot_exit(1);
	}
if (wrd[2][0] && wrd[2][0]!='#') {
	fprintf(stderr,"NUTS: Unexpected word following init parameter on line %d.\n",config_line);
	boot_exit(1);
	}
val=atoi(wrd[1]);
switch(op) {
	case 0: /* main port */
	case 1:
	case 2:
	if ((port[op]=val)<1 || val>65535) {
		fprintf(stderr,"NUTS: Illegal port number on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 3:  
	if ((system_logging=onoff_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: System_logging must be ON or OFF on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 4:
	if ((minlogin_level=get_level(wrd[1]))==-1) {
		if (strcmp(wrd[1],"NONE")) {
			fprintf(stderr,"NUTS: Unknown level specifier for minlogin_level on line %d.\n",config_line);
			boot_exit(1);	
			}
		minlogin_level=-1;
		}
	return;

	case 5:  /* message lifetime */
	if ((mesg_life=val)<1) {
		fprintf(stderr,"NUTS: Illegal message lifetime on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 6: /* wizport_level */
	if ((wizport_level=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier for wizport_level on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 7: /* prompt defaults */
	if ((prompt_def=onoff_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Prompt_def must be ON or OFF on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 8: /* gatecrash level */
	if ((gatecrash_level=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier for gatecrash_level on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 9:
	if (val<1) {
		fprintf(stderr,"NUTS: Number too low for min_private_users on line %d.\n",config_line);
		boot_exit(1);
		}
	min_private_users=val;
	return;

	case 10:
	if ((ignore_mp_level=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier for ignore_mp_level on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 11: 
	/* Max level a remote user can remotely log in if he doesn't have a local
	   account. ie if level set to WIZARD a SU can only be a WIZARD if logging in 
	   from another server unless he has a local account of level SU */
	if ((rem_user_maxlevel=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier for rem_user_maxlevel on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 12:
	/* Default level of remote user who does not have an account on site and
	   connection is from a server of version 3.3.0 or lower. */
	if ((rem_user_deflevel=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier for rem_user_deflevel on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 13:
	if (strlen(wrd[1])>VERIFY_LEN) {
		fprintf(stderr,"NUTS: Verification too long on line %d.\n",config_line);
		boot_exit(1);	
		}
	strcpy(verification,wrd[1]);
	return;

	case 14: /* mesg_check_time */
	if (wrd[1][2]!=':'
	    || strlen(wrd[1])>5
	    || !isdigit(wrd[1][0]) 
	    || !isdigit(wrd[1][1])
	    || !isdigit(wrd[1][3]) 
	    || !isdigit(wrd[1][4])) {
		fprintf(stderr,"NUTS: Invalid message check time on line %d.\n",config_line);
		boot_exit(1);
		}
	sscanf(wrd[1],"%d:%d",&mesg_check_hour,&mesg_check_min);
	if (mesg_check_hour>23 || mesg_check_min>59) {
		fprintf(stderr,"NUTS: Invalid message check time on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 15:
	if ((max_users=val)<1) {
		fprintf(stderr,"NUTS: Invalid value for max_users on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 16:
	if ((heartbeat=val)<1) {
		fprintf(stderr,"NUTS: Invalid value for heartbeat on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 17:
	if ((login_idle_time=val)<10) {
		fprintf(stderr,"NUTS: Invalid value for login_idle_time on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 18:
	if ((user_idle_time=val)<10) {
		fprintf(stderr,"NUTS: Invalid value for user_idle_time on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 19: 
	if ((password_echo=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Password_echo must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 20: 
	if ((ignore_sigterm=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Ignore_sigterm must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 21:
	if ((auto_connect=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Auto_connect must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 22:
	if ((max_clones=val)<0) {
		fprintf(stderr,"NUTS: Invalid value for max_clones on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 23:
	if ((ban_swearing=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Ban_swearing must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 24:
	if (!strcmp(wrd[1],"NONE")) crash_action=0;
	else if (!strcmp(wrd[1],"IGNORE")) crash_action=1;
		else if (!strcmp(wrd[1],"REBOOT")) crash_action=2;
			else {
				fprintf(stderr,"NUTS: Crash_action must be NONE, IGNORE or REBOOT on line %d.\n",config_line);
				boot_exit(1);
				}
	return;

	case 25:
	if ((colour_def=onoff_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Colour_def must be ON or OFF on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 26:
	if ((time_out_afks=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Time_out_afks must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 27:
	if ((allow_caps_in_name=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Allow_caps_in_name must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 28:
	if ((charecho_def=onoff_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Charecho_def must be ON or OFF on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 29:
	if ((time_out_maxlevel=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier for time_out_maxlevel on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;
	}
}



/*** Parse rooms section ***/
parse_rooms_section()
{
static int in_section=0;
int i;
char *ptr1,*ptr2,c;
RM_OBJECT room;

if (!strcmp(wrd[0],"ROOMS:")) {
	if (++in_section>1) {
		fprintf(stderr,"NUTS: Unexpected ROOMS section header on line %d.\n",config_line);
		boot_exit(1);
		}
	return;
	}
if (!wrd[2][0]) {
	fprintf(stderr,"NUTS: Required parameter(s) missing on line %d.\n",config_line);
	boot_exit(1);
	}
if (strlen(wrd[0])>ROOM_LABEL_LEN) {
	fprintf(stderr,"NUTS: Room label too long on line %d.\n",config_line);
	boot_exit(1);
	}
if (strlen(wrd[1])>ROOM_NAME_LEN) {
	fprintf(stderr,"NUTS: Room name too long on line %d.\n",config_line);
	boot_exit(1);
	}
/* Check for duplicate label or name */
for(room=room_first;room!=NULL;room=room->next) {
	if (!strcmp(room->label,wrd[0])) {
		fprintf(stderr,"NUTS: Duplicate room label on line %d.\n",config_line);
		boot_exit(1);
		}
	if (!strcmp(room->name,wrd[1])) {
		fprintf(stderr,"NUTS: Duplicate room name on line %d.\n",config_line);
		boot_exit(1);
		}
	}
room=create_room();
strcpy(room->label,wrd[0]);
strcpy(room->name,wrd[1]);
if (!strcmp(wrd[1], "jail")) jail=room;

/* Parse internal links bit ie hl,gd,of etc. MUST NOT be any spaces between
   the commas */
i=0;
ptr1=wrd[2];
ptr2=wrd[2];
while(1) {
	while(*ptr2!=',' && *
ptr2!='\0') ++ptr2;
	if (*ptr2==',' && *(ptr2+1)=='\0') {
		fprintf(stderr,"NUTS: Missing link label on line %d.\n",config_line);
		boot_exit(1);
		}
	c=*ptr2;  *ptr2='\0';
	if (!strcmp(ptr1,room->label)) {
		fprintf(stderr,"NUTS: Room has a link to itself on line %d.\n",config_line);
		boot_exit(1);
		}
	strcpy(room->link_label[i],ptr1);
	if (c=='\0') break;
	if (++i>=MAX_LINKS) {
		fprintf(stderr,"NUTS: Too many links on line %d.\n",config_line);
		boot_exit(1);
		}
	*ptr2=c;
	ptr1=++ptr2;  
	}

/* Parse access privs */
if (wrd[3][0]=='#') {  room->access=PUBLIC;  return;  }
if (!wrd[3][0] || !strcmp(wrd[3],"BOTH")) room->access=PUBLIC; 
else if (!strcmp(wrd[3],"PUB")) room->access=FIXED_PUBLIC; 
	else if (!strcmp(wrd[3],"PRIV")) room->access=FIXED_PRIVATE;
		else {
			fprintf(stderr,"NUTS: Unknown room access type on line %d.\n",config_line);
			boot_exit(1);
			}
/* Parse external link stuff */
if (!wrd[4][0] || wrd[4][0]=='#') return;
if (!strcmp(wrd[4],"ACCEPT")) {  
	if (wrd[5][0] && wrd[5][0]!='#') {
		fprintf(stderr,"NUTS: Unexpected word following ACCEPT keyword on line %d.\n",config_line);
		boot_exit(1);
		}
	room->inlink=1;  
	return;
	}
if (!strcmp(wrd[4],"CONNECT")) {
	if (!wrd[5][0]) {
		fprintf(stderr,"NUTS: External link name missing on line %d.\n",config_line);
		boot_exit(1);
		}
	if (wrd[6][0] && wrd[6][0]!='#') {
		fprintf(stderr,"NUTS: Unexpected word following external link name on line %d.\n",config_line);
		boot_exit(1);
		}
	strcpy(room->netlink_name,wrd[5]);
	return;
	}
fprintf(stderr,"NUTS: Unknown connection option on line %d.\n",config_line);
boot_exit(1);
}



/*** Parse sites section ***/
parse_sites_section()
{
NL_OBJECT nl;
static int in_section=0;

if (!strcmp(wrd[0],"SITES:")) { 
	if (++in_section>1) {
		fprintf(stderr,"NUTS: Unexpected SITES section header on line %d.\n",config_line);
		boot_exit(1);
		}
	return;
	}
if (!wrd[3][0]) {
	fprintf(stderr,"NUTS: Required parameter(s) missing on line %d.\n",config_line);
	boot_exit(1);
	}
if (strlen(wrd[0])>SERV_NAME_LEN) {
	fprintf(stderr,"NUTS: Link name length too long on line %d.\n",config_line);
	boot_exit(1);
	}
if (strlen(wrd[3])>VERIFY_LEN) {
	fprintf(stderr,"NUTS: Verification too long on line %d.\n",config_line);
	boot_exit(1);
	}
if ((nl=create_netlink())==NULL) {
	fprintf(stderr,"NUTS: Memory allocation failure creating netlink on line %d.\n",config_line);
	boot_exit(1);
	}
if (!wrd[4][0] || wrd[4][0]=='#' || !strcmp(wrd[4],"ALL")) nl->allow=ALL;
else if (!strcmp(wrd[4],"IN")) nl->allow=IN;
	else if (!strcmp(wrd[4],"OUT")) nl->allow=OUT;
		else {
			fprintf(stderr,"NUTS: Unknown netlink access type on line %d.\n",config_line);
			boot_exit(1);
			}
if ((nl->port=atoi(wrd[2]))<1 || nl->port>65535) {
	fprintf(stderr,"NUTS: Illegal port number on line %d.\n",config_line);
	boot_exit(1);
	}
strcpy(nl->service,wrd[0]);
strtolower(wrd[1]);
strcpy(nl->site,wrd[1]);
strcpy(nl->verification,wrd[3]);
}


yn_check(wd)
char *wd;
{
if (!strcmp(wd,"YES")) return 1;
if (!strcmp(wd,"NO")) return 0;
return -1;
}


onoff_check(wd)
char *wd;
{
if (!strcmp(wd,"ON")) return 1;
if (!strcmp(wd,"OFF")) return 0;
return -1;
}


/************ INITIALISATION FUNCTIONS *************/

/*** Initialise globals ***/
init_globals()
{
verification[0]='\0';
port[0]=0;
port[1]=0;
port[2]=0;
auto_connect=1;
max_users=50;
max_clones=1;
ban_swearing=0;
heartbeat=2;
keepalive_interval=60; /* DO NOT TOUCH!!! */
net_idle_time=300; /* Must be > than the above */
login_idle_time=180;
user_idle_time=300;
time_out_afks=0;
wizport_level=JRWIZ;
minlogin_level=-1;
mesg_life=1;
no_prompt=0;
num_of_users=0;
num_of_logins=0;
system_logging=1;
password_echo=0;
ignore_sigterm=0;
crash_action=2;
prompt_def=1;
colour_def=1;
charecho_def=0;
time_out_maxlevel=USER;
mesg_check_hour=0;
mesg_check_min=0;
allow_caps_in_name=1;
rs_countdown=0;
rs_announce=0;
rs_which=-1;
rs_user=NULL;
gatecrash_level=SU+1; /* minimum user level which can enter private rooms */
min_private_users=2; /* minimum num. of users in room before can set to priv */
ignore_mp_level=SU; /* User level which can ignore the above var. */
rem_user_maxlevel=USER;
rem_user_deflevel=USER;
user_first=NULL;
user_last=NULL;
room_first=NULL;
room_last=NULL; /* This variable isn't used yet */
nl_first=NULL;
nl_last=NULL;
clear_words();
time(&boot_time);
}


/*** Initialise the signal traps etc ***/
init_signals()
{
void sig_handler();

signal(SIGTERM,sig_handler);
signal(SIGSEGV,sig_handler);
signal(SIGBUS,sig_handler);
signal(SIGILL,SIG_IGN);
signal(SIGTRAP,SIG_IGN);
signal(SIGIOT,SIG_IGN);
signal(SIGTSTP,SIG_IGN);
signal(SIGCONT,SIG_IGN);
signal(SIGHUP,SIG_IGN);
signal(SIGINT,SIG_IGN);
signal(SIGQUIT,SIG_IGN);
signal(SIGABRT,SIG_IGN);
signal(SIGFPE,SIG_IGN);
signal(SIGPIPE,SIG_IGN);
signal(SIGTTIN,SIG_IGN);
signal(SIGTTOU,SIG_IGN);
}


/*** Talker signal handler function. Can either shutdown , ignore or reboot
	if a unix error occurs though if we ignore it we're living on borrowed
	time as usually it will crash completely after a while anyway. ***/
void sig_handler(sig)
int sig;
{
force_listen=1;
switch(sig) {
	case SIGTERM:
	if (ignore_sigterm) {
		write_syslog("SIGTERM signal received - ignoring.\n",1);
		return;
		}
	write_room(NULL,"\n\n[ ~OLSYSTEM:~FR~LI SIGTERM received, initiating shutdown! ]\n\n");
	talker_shutdown(NULL,"a termination signal (SIGTERM)",0); 

	case SIGSEGV:
	switch(crash_action) {
		case 0:	
		write_room(NULL,"\n\n\07~OL[ SYSTEM:~FR~LI PANIC - Segmentation fault, initiating shutdown! ]\n\n");
		talker_shutdown(NULL,"a segmentation fault (SIGSEGV)",0); 

		case 1:	
		write_room(NULL,"\n\n\07[ ~OLSYSTEM:~FR~LI WARNING - A segmentation fault has just occurred! ]\n\n");
		write_syslog("WARNING: A segmentation fault occurred!\n",1);
		longjmp(jmpvar,0);

		case 2:
		write_room(NULL,"\n\n\07[ ~OLSYSTEM:~FR~LI PANIC - Segmentation fault, initiating reboot! ]\n\n");
		talker_shutdown(NULL,"a segmentation fault (SIGSEGV)",1); 
		}

	case SIGBUS:
	switch(crash_action) {
		case 0:
		write_room(NULL,"\n\n\07[ ~OLSYSTEM:~FR~LI PANIC - Bus error, initiating shutdown! ]\n\n");
		talker_shutdown(NULL,"a bus error (SIGBUS)",0);

		case 1:
		write_room(NULL,"\n\n\07[ ~OLSYSTEM:~FR~LI WARNING - A bus error has just occurred! ]\n\n");
		write_syslog("WARNING: A bus error occurred!\n",1);
		longjmp(jmpvar,0);

		case 2:
		write_room(NULL,"\n\n\07[ ~OLSYSTEM:~FR~LI PANIC - Bus error, initiating reboot! ]\n\n");
		talker_shutdown(NULL,"a bus error (SIGBUS)",1);
		}
	}
}

	
/*** Initialise sockets on ports ***/
init_sockets()
{
struct sockaddr_in bind_addr;
int i,on,size;

printf("Initialising sockets on ports: %d, %d, %d\n",port[0],port[1],port[2]);
on=1;
size=sizeof(struct sockaddr_in);
bind_addr.sin_family=AF_INET;
bind_addr.sin_addr.s_addr=INADDR_ANY;
for(i=0;i<3;++i) {
	/* create sockets */
	if ((listen_sock[i]=socket(AF_INET,SOCK_STREAM,0))==-1) boot_exit(i+2);

	/* allow reboots on port even with TIME_WAITS */
	setsockopt(listen_sock[i],SOL_SOCKET,SO_REUSEADDR,(char *)&on,sizeof(on));

	/* bind sockets and set up listen queues */
	bind_addr.sin_port=htons(port[i]);
	if (bind(listen_sock[i],(struct sockaddr *)&bind_addr,size)==-1) 
		boot_exit(i+5);
	if (listen(listen_sock[i],10)==-1) boot_exit(i+8);

	/* Set to non-blocking , do we need this? Not really. */
	fcntl(listen_sock[i],F_SETFL,O_NDELAY);
	}
}


/*** Initialise connections to remote servers. Basically this tries to connect
     to the services listed in the config file and it puts the open sockets in 
	the NL_OBJECT linked list which the talker then uses ***/
init_connections()
{
NL_OBJECT nl;
RM_OBJECT rm;
int ret,cnt=0;

printf("Connecting to remote servers...\n");
errno=0;
for(rm=room_first;rm!=NULL;rm=rm->next) {
	if ((nl=rm->netlink)==NULL) continue;
	++cnt;
	printf("  Trying service %s at %s %d: ",nl->service,nl->site,nl->port);
	fflush(stdout);
	if ((ret=connect_to_site(nl))) {
		if (ret==1) {
			printf("%s.\n",sys_errlist[errno]);
			sprintf(text,"NETLINK: Failed to connect to %s: %s.\n",nl->service,sys_errlist[errno]);
			}
		else {
			printf("Unknown hostname.\n");
			sprintf(text,"NETLINK: Failed to connect to %s: Unknown hostname.\n",nl->service);
			}
		write_syslog(text,1);
		}
	else {
		printf("CONNECTED.\n");
		sprintf(text,"NETLINK: Connected to %s (%s %d).\n",nl->service,nl->site,nl->port);
		write_syslog(text,1);
		nl->connect_room=rm;
		}
	}
if (cnt) printf("  See system log for any further information.\n");
else printf("  No remote connections configured.\n");
}


/*** Do the actual connection ***/
connect_to_site(nl)
NL_OBJECT nl;
{
struct sockaddr_in con_addr;
struct hostent *he;
int inetnum;
char *sn;

sn=nl->site;
/* See if number address */
while(*sn && (*sn=='.' || isdigit(*sn))) sn++;

/* Name address given */
if(*sn) {
	if(!(he=gethostbyname(nl->site))) return 2;
	memcpy((char *)&con_addr.sin_addr,he->h_addr,(size_t)he->h_length);
	}
/* Number address given */
else {
	if((inetnum=inet_addr(nl->site))==-1) return 1;
	memcpy((char *)&con_addr.sin_addr,(char *)&inetnum,(size_t)sizeof(inetnum));
	}
/* Set up stuff and disable interrupts */
if ((nl->socket=socket(AF_INET,SOCK_STREAM,0))==-1) return 1;
con_addr.sin_family=AF_INET;
con_addr.sin_port=htons(nl->port);
signal(SIGALRM,SIG_IGN);

/* Attempt the connect. This is where the talker may hang. */
if (connect(nl->socket,(struct sockaddr *)&con_addr,sizeof(con_addr))==-1) {
	reset_alarm();  return 1;
	}
reset_alarm();
nl->type=OUTGOING;
nl->stage=VERIFYING;
nl->last_recvd=time(0);
return 0;
}

	

/************* WRITE FUNCTIONS ************/

/*** Write a NULL terminated string to a socket ***/
write_sock(sock,str)
int sock;
char *str;
{
write(sock,str,strlen(str));
}



/*** Send message to user ***/
write_user(user,str)
UR_OBJECT user;
char *str;
{
int buffpos,sock,i;
char *start,buff[OUT_BUFF_SIZE],mesg[ARR_SIZE],*colour_com_strip();

if (user==NULL) return;
if (user->type==REMOTE_TYPE) {
	if (user->netlink->ver_major<=3 
	    && user->netlink->ver_minor<2) str=colour_com_strip(str);
	if (str[strlen(str)-1]!='\n') 
		sprintf(mesg,"MSG %s\n%s\nEMSG\n",user->name,str);
	else sprintf(mesg,"MSG %s\n%sEMSG\n",user->name,str);
	write_sock(user->netlink->socket,mesg);
	return;
	}
start=str;
buffpos=0;
sock=user->socket;
/* Process string and write to buffer. We use pointers here instead of arrays 
   since these are supposedly much faster (though in reality I guess it depends
   on the compiler) which is necessary since this routine is used all the 
   time. */
while(*str) {
	if (*str=='\n') {
		if (buffpos>OUT_BUFF_SIZE-6) {
			write(sock,buff,buffpos);  buffpos=0;
			}
		/* Reset terminal before every newline */
		if (user->colour) {
			memcpy(buff+buffpos,"\033[0m",4);  buffpos+=4;
			}
		*(buff+buffpos)='\n';  *(buff+buffpos+1)='\r';  
		buffpos+=2;  ++str;
		}
	else {  
		/* See if its a / before a ~ , if so then we print colour command
		   as text */
		if (*str=='/' && *(str+1)=='~') {  ++str;  continue;  }
		if (str!=start && *str=='~' && *(str-1)=='/') {
			*(buff+buffpos)=*str;  goto CONT;
			}
		/* Process colour commands eg ~FR. We have to strip out the commands 
		   from the string even if user doesnt have colour switched on hence 
		   the user->colour check isnt done just yet */
		if (*str=='~') {
			if (buffpos>OUT_BUFF_SIZE-6) {
				write(sock,buff,buffpos);  buffpos=0;
				}
			++str;
			for(i=0;i<NUM_COLS;++i) {
				if (!strncmp(str,colcom[i],2)) {
					if (user->colour) {
						memcpy(buff+buffpos,colcode[i],strlen(colcode[i]));
						buffpos+=strlen(colcode[i])-1;  
						}
					else buffpos--;
					++str;
					goto CONT;
					}
				}
			*(buff+buffpos)=*(--str);
			}
		else *(buff+buffpos)=*str;
		CONT:
		++buffpos;   ++str; 
		}
	if (buffpos==OUT_BUFF_SIZE) {
		write(sock,buff,OUT_BUFF_SIZE);  buffpos=0;
		}
	}
if (buffpos) write(sock,buff,buffpos);
/* Reset terminal at end of string */
if (user->colour) write_sock(sock,"\033[0m"); 
}



/*** Write to users of level 'level' and above or below depending on above
     variable; if 1 then above else below ***/
write_level(level,above,str,user)
int level,above;
char *str;
UR_OBJECT user;
{
UR_OBJECT u;

for(u=user_first;u!=NULL;u=u->next) {
	if (u!=user && !u->login && u->type!=CLONE_TYPE) {
		if ((above && u->level>=level) || (!above && u->level<=level)) 
			write_user(u,str);
		}
	}
}



/*** Subsid function to below but this one is used the most ***/
write_room(rm,str)
RM_OBJECT rm;
char *str;
{
write_room_except(rm,str,NULL);
}



/*** Write to everyone in room rm except for "user". If rm is NULL write 
     to all rooms. ***/
write_room_except(rm,str,user)
RM_OBJECT rm;
char *str;
UR_OBJECT user;
{
UR_OBJECT u;
char text2[ARR_SIZE];

for(u=user_first;u!=NULL;u=u->next) {
	if (u->login 
	    || u->room==NULL 
	    || (u->room!=rm && rm!=NULL) 
	    || (u->ignall && !force_listen)
	    || (u->ignshout && (com_num==SHOUT))
	    || (u->ignchat && (com_num==CHAT || com_num==CHATEMOTE))
	    || (u->ignwiz && (com_num==WIZ || com_num==WIZEMOTE))
	    || (u->ignhonor && (com_num==HONOR || com_num==HONOREMOTE))
	    || (u->ignadmin && (com_num==ADMIN || com_num==ADMINEMOTE))
	    || (u->igngod && (com_num==GOD || com_num==GODEMOTE))
	    || u==user) continue;
	if (u->type==CLONE_TYPE) {
		if (u->clone_hear==CLONE_HEAR_NOTHING || u->owner->ignall) continue;
		/* Ignore anything not in clones room, eg shouts, system messages
		   since the clones owner will hear them anyway. */
		if (rm!=u->room) continue;
		if (u->clone_hear==CLONE_HEAR_SWEARS) {
			if (!contains_swearing(str)) continue;
			}
		sprintf(text2,"~FT[ %s ]:~RS %s",u->room->name,str);
		write_user(u->owner,text2);
		}
	else write_user(u,str); 
	}
}



/*** Write a string to system log ***/
write_syslog(str,write_time)
char *str;
int write_time;
{
FILE *fp;

if (!system_logging || !(fp=fopen(SYSLOG,"a"))) return;
if (!write_time) fputs(str,fp);
else fprintf(fp,"%02d/%02d %02d:%02d:%02d: %s",tmday,tmonth+1,thour,tmin,tsec,str);
fclose(fp);
}



/******** LOGIN/LOGOUT FUNCTIONS ********/

/*** Login function. Lots of nice inline code :) ***/
login(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
UR_OBJECT u;
int i;
char name[ARR_SIZE],passwd[ARR_SIZE];

name[0]='\0';  passwd[0]='\0';
switch(user->login) {
	case 3:
	sscanf(inpstr,"%s",name);
	if(name[0]<33) {
		write_user(user,"\nEnter a name: ");  return;
		}
	if (!strcmp(name,"quit")) {
		write_user(user,"\n\n*** Abandoning login attempt ***\n\n");
		disconnect_user(user);  return;
		}
	if (!strcmp(name,"who")) {
		who(user,0);  
		write_user(user,"\nEnter a name: ");
		return;
		}
	if (!strcmp(name,"version")) {
		sprintf(text,"\nBolts version %s (NUTS %s based)\n\nEnter a name: ",boltsversion,VERSION);
		write_user(user,text);  return;
		}
	if (strlen(name)<3) {
		write_user(user,"\nName too short.\n\n");  
		attempts(user);  return;
		}
	if (strlen(name)>USER_NAME_LEN) {
		write_user(user,"\nName too long.\n\n");
		attempts(user);  return;
		}
	/* see if only letters in login */
	for (i=0;i<strlen(name);++i) {
		if (!isalpha(name[i])) {
			write_user(user,"\nOnly letters are allowed in a name.\n\n");
			attempts(user);  return;
			}
		}
	if (!allow_caps_in_name) strtolower(name);
	name[0]=toupper(name[0]);
	if (contains_swearing(name)) {
                write_user(user,"\nYou cannot use a name like that, sorry!\n\n");
                attempts(user);  return;
	      }
	if (!allow_caps_in_name) strtolower(name);
	name[0]=toupper(name[0]);

	/* Prelogins get shown to wiz & above */
	sprintf(text,"~OLLogging on:~RS %s ~FT(Site %s : Port: %d : Talker Port %d)\n",name,user->site,user->site_port,user->port);
	write_level(WIZARD,1,text,NULL);

	if (user_banned(name)) {
		write_user(user,"\nYou are banned from this talker.\n\n");
		disconnect_user(user);
		sprintf(text,"~OL[ Attempted login by banned name %s ]~RS\n",name);
		write_level(ARCH,1,text,NULL);
		sprintf(text,"Attempted login by banned user %s.\n",name);
		write_syslog(text,1);
		return;
		}
	strcpy(user->name,name);
	/* If user has hung on another login clear that session */
	for(u=user_first;u!=NULL;u=u->next) {
		if (u->login && u!=user && !strcmp(u->name,user->name)) {
			disconnect_user(u);  break;
			}
		}	
	if (!load_user_details(user)) {
		if (user->port==port[1]) {
			write_user(user,"\nSorry, new logins cannot be created on this port.\n\n");
			disconnect_user(user);  
			return;
			}
		if (minlogin_level>-1) {
			write_user(user,"\nSorry, new logins cannot be created at this time.\n\n");
			disconnect_user(user);  
			return;
			}
		write_user(user,"Ah, a new user.\n");
		}
	else {
		if (user->port==port[1] && user->level<wizport_level) {
			sprintf(text,"\nSorry, only users of level %s and above can log in on this port.\n\n",level_name[wizport_level]);
			write_user(user,text);
			disconnect_user(user);  
			return;
			}
		if (user->level<minlogin_level) {
			write_user(user,"\nSorry, the talker is locked out to users of your level.\n\n");
			disconnect_user(user);  
			return;
			}
		}
	write_user(user,"Enter a password: ");
	echo_off(user);
	user->login=2;
	return;

	case 2:
	sscanf(inpstr,"%s",passwd);
	if (strlen(passwd)<3) {
		write_user(user,"\n\nPassword too short.\n\n");  
		attempts(user);  return;
		}
	if (strlen(passwd)>PASS_LEN) {
		write_user(user,"\n\nPassword too long.\n\n");
		attempts(user);  return;
		}
	/* if new user... */
	if (!user->pass[0]) {
		strcpy(user->pass,(char *)crypt(passwd,"NU"));
		write_user(user,"\nPlease confirm password: ");
		user->login=1;
		}
	else {
		if (!strcmp(user->pass,(char *)crypt(passwd,"NU"))) {
			echo_on(user);  connect_user(user);  return;
			}
		write_user(user,"\n\nIncorrect login.\n\n");
		attempts(user);
		}
	return;

	case 1:
	sscanf(inpstr,"%s",passwd);
	if (strcmp(user->pass,(char*)crypt(passwd,"NU"))) {
		write_user(user,"\n\nPasswords do not match.\n\n");
		attempts(user);
		return;
		}
	echo_on(user);
	strcpy(user->desc,"hasn't used .desc yet.");
	strcpy(user->in_phrase,"enters");
	strcpy(user->out_phrase,"goes");	
	user->age=0;
	user->gender=0;
	strcpy(user->icq,"-Unset-");	
	strcpy(user->url,"-Unset-");	
	strcpy(user->email,"-Unset-");
	user->hidemail=0;
	user->autofwd=0;
	user->twin=0;
	user->tlose=0;
	user->tdraw=0;
	user->hang_word[0]='\0';
	user->hang_guess[0]='\0';
	user->hang_word_show[0]='\0';
	user->hang_stage=-1;
	user->puzzle=NULL;
	user->bank_balance=0;
	user->last_site[0]='\0';
	user->level=1;
	user->muzzled=0;
	user->command_mode=0;
	user->prompt=prompt_def;
	user->colour=colour_def;
	user->charmode_echo=charecho_def;
	save_user_details(user,1);
	sprintf(text,"New user \"%s\" created.\n",user->name);
	write_syslog(text,1);
	connect_user(user);
	}
}
	


/*** Count up attempts made by user to login ***/
attempts(user)
UR_OBJECT user;
{
user->attempts++;
if (user->attempts==3) {
	write_user(user,"\nMaximum attempts reached.\n\n");
	disconnect_user(user);  return;
	}
user->login=3;
user->pass[0]='\0';
write_user(user,"Enter a name: ");
echo_on(user);
}



/*** Load the users details ***/
load_user_details(user)
UR_OBJECT user;
{
FILE *fp;
char line[81],filename[80];
int temp1,temp2,temp3;

sprintf(filename,"%s/%s.D",USERFILES,user->name);
if (!(fp=fopen(filename,"r"))) return 0;

fscanf(fp,"%s",user->pass); /* password */
fscanf(fp,"%d %d %d %d %d %d %d %d %d %d",&temp1,&temp2,&user->last_login_len,&temp3,&user->level,&user->prompt,&user->muzzled,&user->charmode_echo,&user->command_mode,&user->colour);
user->last_login=(time_t)temp1;
user->total_login=(time_t)temp2;
user->read_mail=(time_t)temp3;
fscanf(fp,"%s\n",user->last_site);

/* Need to do the rest like this 'cos they may be more than 1 word each */
fgets(line,USER_DESC_LEN+2,fp);
line[strlen(line)-1]=0;
strcpy(user->desc,line); 
fgets(line,PHRASE_LEN+2,fp);
line[strlen(line)-1]=0;
strcpy(user->in_phrase,line); 
fgets(line,PHRASE_LEN+2,fp);
line[strlen(line)-1]=0;
strcpy(user->out_phrase,line); 
if (!feof(fp)) fscanf(fp, "%d", &user->arrested); else user->arrested=0;
fscanf(fp, "%d", &user->vis);
fscanf(fp, "%s", &user->icq);
fscanf(fp, "%s", &user->url);
fscanf(fp, "%d %d", &user->age,&user->gender);
fscanf(fp, "%s", &user->email);
fscanf(fp, "%d %d", &user->hidemail,&user->autofwd);
fscanf(fp, "%d %d %d", &user->twin,&user->tlose,&user->tdraw);
fscanf(fp, "%d", &user->bank_balance);
fclose(fp);
return 1;
}



/*** Save a users stats ***/
save_user_details(user,save_current)
UR_OBJECT user;
int save_current;
{
FILE *fp;
char filename[80];

if (user->type==REMOTE_TYPE || user->type==CLONE_TYPE) return 0;
sprintf(filename,"%s/%s.D",USERFILES,user->name);
if (!(fp=fopen(filename,"w"))) {
	sprintf(text,"%s: failed to save your details.\n",syserror);	
	write_user(user,text);
	sprintf(text,"SAVE_USER_STATS: Failed to save %s's details.\n",user->name);
	write_syslog(text,1);
	return 0;
	}
fprintf(fp,"%s\n",user->pass);
if (save_current)
	fprintf(fp,"%d %d %d ",(int)time(0),(int)user->total_login,(int)(time(0)-user->last_login));
else fprintf(fp,"%d %d %d ",(int)user->last_login,(int)user->total_login,user->last_login_len);
fprintf(fp,"%d %d %d %d %d %d %d\n",(int)user->read_mail,user->level,user->prompt,user->muzzled,user->charmode_echo,user->command_mode,user->colour);
if (save_current) fprintf(fp,"%s\n",user->site);
else fprintf(fp,"%s\n",user->last_site);
fprintf(fp,"%s\n",user->desc);
fprintf(fp,"%s\n",user->in_phrase);
fprintf(fp,"%s\n",user->out_phrase);
fprintf(fp, "%d\n", user->arrested);
fprintf(fp,"%d\n",user->vis);
fprintf(fp,"%s\n",user->icq);
fprintf(fp,"%s\n",user->url);
fprintf(fp,"%d %d\n",user->age,user->gender);
fprintf(fp,"%s\n",user->email);
fprintf(fp,"%d %d\n",user->hidemail,user->autofwd);
fprintf(fp,"%d %d %d\n",user->twin,user->tlose,user->tdraw);
fprintf(fp,"%d\n",user->bank_balance);
fclose(fp);
return 1;
}


/*** Connect the user to the talker proper ***/
connect_user(user)
UR_OBJECT user;
{
UR_OBJECT u,u2;
RM_OBJECT rm;
char temp[30],*name;

/* See if user already connected */
for(u=user_first;u!=NULL;u=u->next) {
	if (user!=u && user->type!=CLONE_TYPE && !strcmp(user->name,u->name)) {
		rm=u->room;
		if (u->type==REMOTE_TYPE) {
			write_user(u,"\n~FB~OLYou are pulled back through cyberspace...\n");
			sprintf(text,"REMVD %s\n",u->name);
			write_sock(u->netlink->socket,text);
			sprintf(text,"%s vanishes.\n",u->name);
			destruct_user(u);
			write_room(rm,text);
			reset_access(rm);
			num_of_users--;
			break;
			}
		write_user(user,"\n\nYou are already connected - switching to old session...\n");
		if (user->vis) name=user->name; else name=invisname;
		sprintf(text,"%s has reconnected.\n",user->name);
		write_syslog(text,1);
		close(u->socket);
		u->socket=user->socket;
		strcpy(u->site,user->site);
		u->site_port=user->site_port;
		destruct_user(user);
		num_of_logins--;
		if (u->vis) {
			sprintf(text,"[ ~OLRECONNECTED:~RS %s(%s) %s ]\n",invisname,levels[u->level],u->desc);
			write_room_except(rm,text,u);
			}
		sprintf(text,"[ ~OLRECONNECTED:~RS %s(%s) %s ]\n",u->name,levels[u->level],u->desc);
		write_level(ARCH,1,text,NULL);
		if (rm==NULL) {
			sprintf(text,"ACT %s look\n",u->name);
			write_sock(u->netlink->socket,text);
			}
		else {
			look(u);  prompt(u);
			}
		/* Reset the sockets on any clones */
		for(u2=user_first;u2!=NULL;u2=u2->next) {
			if (u2->type==CLONE_TYPE && u2->owner==user) {
				u2->socket=u->socket;  u->owner=u;
				}
			}
		return;
		}
	}
/* Announce users logon. You're probably wondering why Ive done it this strange
   way , well its to avoid a minor bug that arose when as in 3.3.1 it created 
   the string in 2 parts and sent the parts seperately over a netlink. If you 
   want more details email me. */	
if (user->vis) {
	sprintf(text,"[ ~OLSIGN ON:~RS %s(%s) %s ]\n",user->name,levels[user->level],user->desc);
	write_level(USER,0,text,NULL);
	sprintf(text,"[ ~OLSIGN ON:~RS %s(%s) %s  ~FT(%s:%d) ]~RS\n",user->name,levels[user->level],user->desc,user->site,user->site_port);
	write_level(JRWIZ,1,text,NULL);
	}
else {
	sprintf(text,"[ ~OLSIGN ON:~RS (invis) %s(%s) %s  ~RS~FT(%s:%d) ]~RS\n",user->name,levels[user->level],user->desc,user->site,user->site_port);
	write_level(ARCH,1,text,NULL);
	}
/* send post-login message and other logon stuff to user */
write_user(user,"\n");
more(user,user->socket,MOTD2); 
if (user->last_site[0]) {
	sprintf(temp,"%s",ctime(&user->last_login));
	temp[strlen(temp)-1]=0;
	sprintf(text,"Welcome to %s, %s...\n\n~BBYou were last logged in on %s from %s.\n\n",talkername,user->name,temp,user->last_site);
	}
else sprintf(text,"Welcome to %s, %s...\n\n",talkername,user->name);
write_user(user,text);
user->room=room_first;
user->last_login=time(0); /* set to now */
sprintf(text,"~FTYour level is:~RS~OL %s\n",level_name[user->level]);
write_user(user,text);
if (user->arrested) user->room=jail;
look(user);
fam_quote(user);
if (has_unread_mail(user)) write_user(user,"\07~FT~OL~LIYou have unread mail. Please read it via the .rmail command then delete it~RS\n~FT~OL~LIimmediately.~RS\n"); 
if (!user->vis) write_user(user,">>>>> You are currently invisible <<<<<\n");
prompt(user);

/* write to syslog and set up some vars */
sprintf(text,"%s logged in on port %d from %s:%d.\n",user->name,user->port,user->site,user->site_port);
write_syslog(text,1);
if (user->vis) num_of_users++;
num_of_logins--;
user->login=0;
}


/*** Disconnect user from talker ***/
disconnect_user(user)
UR_OBJECT user;
{
RM_OBJECT rm;
NL_OBJECT nl;
int onfor,hours,mins;

rm=user->room;
if (user->login) {
	close(user->socket);  
	destruct_user(user);
	num_of_logins--;  
	return;
	}
if (user->type!=REMOTE_TYPE) {
        onfor=(int)(time(0)-user->last_login);
	hours=(onfor%86400)/3600;
	mins=(onfor%3600)/60;
	save_user_details(user,1);  
	sprintf(text,"%s logged out.\n",user->name);
	write_syslog(text,1,SYSLOG);
	write_user(user,"\n~OL~FBSigning off...\n\n");
	sprintf(text,"You were logged on from site %s\n",user->site);
	write_user(user,text);
	sprintf(text,"On %s, %d %s, for a total of %d hours and %d minutes.\n\n",day[twday],tmday,month[tmonth],hours,mins);
	write_user(user,text);
	close(user->socket);
	if (user->vis) sprintf(text,"[ ~OLSIGN OFF:~RS %s(%s) %s ]\n",user->name,levels[user->level],user->desc);
	write_level(USER,0,text,NULL);
	if (!user->vis) sprintf(text,"[ ~OLSIGN OFF:~RS %s(%s)(invis) %s ]\n",user->name,levels[user->level],user->desc);
	else sprintf(text,"[ ~OLSIGN OFF:~RS %s(%s) %s ]\n",user->name,levels[user->level],user->desc);
	write_level(ARCH,1,text,NULL);
	if (user->room==NULL) {
		sprintf(text,"REL %s\n",user->name);
		write_sock(user->netlink->socket,text);
		for(nl=nl_first;nl!=NULL;nl=nl->next) 
			if (nl->mesg_user==user) {  
				nl->mesg_user=(UR_OBJECT)-1;  break;  
				}
		}
	}
else {
	write_user(user,"\n~FR~OLYou are pulled back in disgrace to your own domain...\n");
	sprintf(text,"REMVD %s\n",user->name);
	write_sock(user->netlink->socket,text);
	sprintf(text,"~FR~OL%s is banished from here!\n",user->name);
	write_room_except(rm,text,user);
	sprintf(text,"NETLINK: Remote user %s removed.\n",user->name);
	write_syslog(text,1);
	}
if (user->malloc_start!=NULL) free(user->malloc_start);
if (user->vis) num_of_users--;
/* Destroy any clones */
destruct_blackjack_game(user);
destroy_user_clones(user);
destruct_user(user);
reset_access(rm);
destructed=0;
}


/*** Tell telnet not to echo characters - for password entry ***/
echo_off(user)
UR_OBJECT user;
{
char seq[4];

if (password_echo) return;
sprintf(seq,"%c%c%c",255,251,1);
write_user(user,seq);
}


/*** Tell telnet to echo characters ***/
echo_on(user)
UR_OBJECT user;
{
char seq[4];

if (password_echo) return;
sprintf(seq,"%c%c%c",255,252,1);
write_user(user,seq);
}



/************ MISCELLANIOUS FUNCTIONS *************/

/*** Stuff that is neither speech nor a command is dealt with here ***/
misc_ops(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
switch(user->misc_op) {
	case 1: 
	if (toupper(inpstr[0])=='Y') {
		if (rs_countdown && !rs_which) {
			if (rs_countdown>60) 
				sprintf(text,"\n\07[ ~OLSYSTEM: ~FR~LISHUTDOWN INITIATED, shutdown in %d minutes, %d seconds! ]\n\n",rs_countdown/60,rs_countdown%60);
			else sprintf(text,"\n\07[ ~OLSYSTEM: ~FR~LISHUTDOWN INITIATED, shutdown in %d seconds! ]\n\n",rs_countdown);
			write_room(NULL,text);
			sprintf(text,"%s initiated a %d seconds SHUTDOWN countdown.\n",user->name,rs_countdown);
			write_syslog(text,1);
			rs_user=user;
			rs_announce=time(0);
			user->misc_op=0;  
			prompt(user);
			return 1;
			}
		talker_shutdown(user,NULL,0); 
		}
	/* This will reset any reboot countdown that was started, oh well */
	rs_countdown=0;
	rs_announce=0;
	rs_which=-1;
	rs_user=NULL;
	user->misc_op=0;  
	prompt(user);
	return 1;

	case 2: 
	if (toupper(inpstr[0])=='E'
	    || more(user,user->socket,user->page_file)!=1) {
		user->misc_op=0;  user->filepos=0;  user->page_file[0]='\0';
		prompt(user); 
		}
	return 1;

	case 3: /* writing on board */
	case 4: /* Writing mail */
	case 5: /* doing profile */
	editor(user,inpstr);  return 1;

	case 6:
	if (toupper(inpstr[0])=='Y') delete_user(user,1); 
	else {  user->misc_op=0;  prompt(user);  }
	return 1;

	case 7:
	if (toupper(inpstr[0])=='Y') {
		if (rs_countdown && rs_which==1) {
			if (rs_countdown>60) 
				sprintf(text,"\n\07[ ~OLSYSTEM: ~FY~LIREBOOT INITIATED, rebooting in %d minutes, %d seconds! ]\n\n",rs_countdown/60,rs_countdown%60);
			else sprintf(text,"\n\07[ ~OLSYSTEM: ~FY~LIREBOOT INITIATED, rebooting in %d seconds! ]\n\n",rs_countdown);
			write_room(NULL,text);
			sprintf(text,"%s initiated a %d seconds REBOOT countdown.\n",user->name,rs_countdown);
			write_syslog(text,1);
			rs_user=user;
			rs_announce=time(0);
			user->misc_op=0;  
			prompt(user);
			return 1;
			}
		talker_shutdown(user,NULL,1); 
		}
	if (rs_which==1 && rs_countdown && rs_user==NULL) {
		rs_countdown=0;
		rs_announce=0;
		rs_which=-1;
		}
	case 8:		/* alert question... */
	if(toupper(inpstr[0])=='Y')
	  {
	    user->afk=0;
	    user->misc_op=0;
	    if (user->vis) {
	    	sprintf(text,"%s escapes from the alien creatures.\n",user->name);
	        write_room_except(user->room,text,user);
		}
	    write_user(user,"You exit from alert mode...\n");
	    prompt(user);
	    return 1;
  	    }
	user->misc_op=0;  
	prompt(user);
	return 1;
	}
return 0;
}


/*** The editor used for writing profiles, mail and messages on the boards ***/
editor(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
int cnt,line,i;
char *edprompt="\nYour options are: ~FG(~OLS~RS~FG)ave~RS, ~FT(~OLV~RS~FT)iew~RS, ~FY(~OLR~RS~FY)edo~RS or ~FR(~OLA~RS~FR)bort~RS: ";
char *ptr,*c;
char text1[25];

if (user->edit_op) {
	switch(toupper(*inpstr)) {
		case 'S':	/* save text */
		if (user->vis) {
			sprintf(text,"%s finishes composing some text.\n",user->name);
			write_room_except(user->room,text,user);
			}
		
		switch(user->misc_op) {
			case 3: write_board(user,NULL,1);  break;
			case 4: smail(user,NULL,1);  break;
			case 5: enter_profile(user,1);	break;
			}
			
		editor_done(user);
		return;

		case 'R':	/* redo editing */
		user->edit_op=0;
		user->edit_line=1;
		user->charcnt=0;
		user->malloc_end=user->malloc_start;
		*user->malloc_start='\0';
		write_user(user,"\nRedo message...\n\n");
	        sprintf(text,"[---------------- Please try to keep between these two markers ----------------]\n\n~FG%d>~RS",user->edit_line);
		write_user(user,text);
		return;

		case 'A':	/* abort editing */
		if (user->vis) sprintf(text,"%s gives up composing some text.\n",user->name);
		write_user(user,"\nMessage aborted.\n");
		editor_done(user);
		return;

		case 'V':	/* view text info */
		switch(user->misc_op)
		  {
		    case 3:strcpy(text1,"message on the board");break;
		    case 4:strcpy(text1,"mail");break;
		    case 5:strcpy(text1,"profile");break;
		    default:strcpy(text1,"unknown letter");
		  }  
		i=0;
		c=user->malloc_start;
		while(c!=user->malloc_end) /* count number of chars */
		  {
		    i++;
		    c++;
		  }    
		sprintf(text,"You entered ~FT%d~RS lines ( ~FT%d~RS characters ) to write a %s.\n",user->edit_line,i,text1);
		write_user(user,text);
	        write_user(user,"\nYou have composed the following text...\n\n");
	        write_user(user,user->malloc_start);
		write_user(user,edprompt);
		return;
		
		default:
		write_user(user,edprompt);
		return;
		}
	}
if (user->malloc_start==NULL) {
	if ((user->malloc_start=(char *)malloc(MAX_LINES*81))==NULL) {
		sprintf(text,"%s: failed to allocate buffer memory.\n",syserror);
		write_user(user,text);
		write_syslog("ERROR: Failed to allocate memory in editor().\n",0);
		user->misc_op=0;
		prompt(user);
		return;
		}
	user->ignall_store=user->ignall;
	user->ignall=1; /* Dont want chat mucking up the edit screen */
	user->edit_line=1;
	user->charcnt=0;
	user->editing=1;
	user->malloc_end=user->malloc_start;
	*user->malloc_start='\0';
	sprintf(text,"~FTMaximum of ~FG%d~FT lines, end with a '~FG.~FT' on a line by itself.\n\n~RS",MAX_LINES);
	write_user(user,text);
	sprintf(text,"[---------------- Please try to keep between these two markers ----------------]\n\n~FG%d>~RS",user->edit_line);
	write_user(user,text);
	if (user->vis) {
		sprintf(text,"%s starts composing some text...\n",user->name);
		write_room_except(user->room,text,user);
		}
	return;
	}

/* Check for empty line */
if (!word_count) {
	if (!user->charcnt) {
		sprintf(text,"~FG%d>~RS",user->edit_line);
		write_user(user,text);
		return;
		}
	*user->malloc_end++='\n';
	if (user->edit_line==MAX_LINES) goto END;
	sprintf(text,"~FG%d>~RS",++user->edit_line);
     	write_user(user,text);
	user->charcnt=0;
     	return;
	}
/* If nothing carried over and a dot is entered then end */
if (!user->charcnt && !strcmp(inpstr,".")) goto END;

line=user->edit_line;
cnt=user->charcnt;

/* loop through input and store in allocated memory */
while(*inpstr) {
	*user->malloc_end++=*inpstr++;
	if (++cnt==80) {  user->edit_line++;  cnt=0;  }
	if (user->edit_line>MAX_LINES
	    || user->malloc_end - user->malloc_start>=MAX_LINES*81) goto END;
	}
if (line!=user->edit_line) {
	ptr=(char *)(user->malloc_end-cnt);
	*user->malloc_end='\0';
	sprintf(text,"~FG%d>~RS%s",user->edit_line,ptr);
	write_user(user,text);
	user->charcnt=cnt;
	return;
	}
else {
	*user->malloc_end++='\n';
	user->charcnt=0;
	}
if (user->edit_line!=MAX_LINES) {
	sprintf(text,"~FG%d>~RS",++user->edit_line);
	write_user(user,text);
	return;
	}

/* User has finished his message , prompt for what to do now */
END:
*user->malloc_end='\0';
if (*user->malloc_start) {
	write_user(user,edprompt);
	user->edit_op=1;  return;
	}
write_user(user,"\nNo text.\n");
if (user->vis) {
	sprintf(text,"%s gives up composing some text.\n",user->name);
	write_room_except(user->room,text,user);
	}
editor_done(user);
}

/*** Reset some values at the end of editing ***/
editor_done(user)
UR_OBJECT user;
{
user->misc_op=0;
user->edit_op=0;
user->edit_line=0;
free(user->malloc_start);
user->malloc_start=NULL;
user->malloc_end=NULL;
user->ignall=user->ignall_store;
user->editing=0;
prompt(user);
}


/*** Record speech and emotes in the room. ***/
record(rm,str)
RM_OBJECT rm;
char *str;
{
strncpy(rm->revbuff[rm->revline],str,REVIEW_LEN);
rm->revbuff[rm->revline][REVIEW_LEN]='\n';
rm->revbuff[rm->revline][REVIEW_LEN+1]='\0';
rm->revline=(rm->revline+1)%REVIEW_LINES;
}


/*** Records tells and pemotes sent to the user. ***/
record_tell(user,str)
UR_OBJECT user;
char *str;
{
strncpy(user->revbuff[user->revline],str,REVIEW_LEN);
user->revbuff[user->revline][REVIEW_LEN]='\n';
user->revbuff[user->revline][REVIEW_LEN+1]='\0';
user->revline=(user->revline+1)%REVTELL_LINES;
}



/*** Set room access back to public if not enough users in room ***/
reset_access(rm)
RM_OBJECT rm;
{
UR_OBJECT u;
int cnt;

if (rm==NULL || rm->access!=PRIVATE) return; 
cnt=0;
for(u=user_first;u!=NULL;u=u->next) if (u->room==rm) ++cnt;
if (cnt<min_private_users) {
	write_room(rm,"Room access returned to ~FGPUBLIC.\n");
	rm->access=PUBLIC;

	/* Reset any invites into the room & clear review buffer */
	for(u=user_first;u!=NULL;u=u->next) {
		if (u->invite_room==rm) u->invite_room=NULL;
		}
	clear_revbuff(rm);
	}
}



/*** Exit because of error during bootup ***/
boot_exit(code)
int code;
{
switch(code) {
	case 1:
	write_syslog("BOOT FAILURE: Error while parsing configuration file.\n",0);
	exit(1);

	case 2:
	perror("NUTS: Can't open main port listen socket");
	write_syslog("BOOT FAILURE: Can't open main port listen socket.\n",0);
	exit(2);

	case 3:
	perror("NUTS: Can't open wiz port listen socket");
	write_syslog("BOOT FAILURE: Can't open wiz port listen socket.\n",0);
	exit(3);

	case 4:
	perror("NUTS: Can't open link port listen socket");
	write_syslog("BOOT FAILURE: Can't open link port listen socket.\n",0);
	exit(4);

	case 5:
	perror("NUTS: Can't bind to main port");
	write_syslog("BOOT FAILURE: Can't bind to main port.\n",0);
	exit(5);

	case 6:
	perror("NUTS: Can't bind to wiz port");
	write_syslog("BOOT FAILURE: Can't bind to wiz port.\n",0);
	exit(6);

	case 7:
	perror("NUTS: Can't bind to link port");
	write_syslog("BOOT FAILURE: Can't bind to link port.\n",0);
	exit(7);
	
	case 8:
	perror("NUTS: Listen error on main port");
	write_syslog("BOOT FAILURE: Listen error on main port.\n",0);
	exit(8);

	case 9:
	perror("NUTS: Listen error on wiz port");
	write_syslog("BOOT FAILURE: Listen error on wiz port.\n",0);
	exit(9);

	case 10:
	perror("NUTS: Listen error on link port");
	write_syslog("BOOT FAILURE: Listen error on link port.\n",0);
	exit(10);

	case 11:
	perror("NUTS: Failed to fork");
	write_syslog("BOOT FAILURE: Failed to fork.\n",0);
	exit(11);
	}
}



/*** User prompt ***/
prompt(user)
UR_OBJECT user;
{
int hr,min;

if (no_prompt) return;
if (user->type==REMOTE_TYPE) {
	sprintf(text,"PRM %s\n",user->name);
	write_sock(user->netlink->socket,text);  
	return;
	}
if (user->command_mode && !user->misc_op) { 
	if (!user->room==0) {
		if (!user->vis) sprintf(text,"~FT%s@%s:/home/%s (invis)> ",user->name,talkername,user->room->name);
		if (user->vis) sprintf(text,"~FT%s@%s:/home/%s> ",user->name,talkername,user->room->name);
		}
	else {
		if (!user->vis) sprintf(text,"~FT%s@%s:/%s/%s (invis)> ",user->name,talkername,user->netlink->service,user->room->name);
		if (user->vis) sprintf(text,"~FT%s@%s:/%s/%s> ",user->name,talkername,user->netlink->service,user->room->name);
		}
	write_user(user,text);
	return;  
	}
if (!user->prompt || user->misc_op) return;
hr=(int)(time(0)-user->last_login)/3600;
min=((int)(time(0)-user->last_login)%3600)/60;
if (!user->vis)
	sprintf(text,"~FT<%02d:%02d, %02d:%02d, %s+>\n",thour,tmin,hr,min,user->name);
else sprintf(text,"~FT<%02d:%02d, %02d:%02d, %s>\n",thour,tmin,hr,min,user->name);
write_user(user,text);
}



/*** Page a file out to user. Colour commands in files will only work if 
     user!=NULL since if NULL we dont know if his terminal can support colour 
     or not. Return values: 
	        0 = cannot find file, 1 = found file, 2 = found and finished ***/
more(user,sock,filename)
UR_OBJECT user;
int sock;
char *filename;
{
int i,buffpos,num_chars,lines,retval,len;
char buff[OUT_BUFF_SIZE],text2[83],*str,*colour_com_strip();
FILE *fp;

if (!(fp=fopen(filename,"r"))) {
	if (user!=NULL) user->filepos=0;  
	return 0;
	}
/* jump to reading posn in file */
if (user!=NULL) fseek(fp,user->filepos,0);

text[0]='\0';  
buffpos=0;  
num_chars=0;
retval=1; 
len=0;

/* If user is remote then only do 1 line at a time */
if (sock==-1) {
	lines=1;  fgets(text2,82,fp);
	}
else {
	lines=0;  fgets(text,sizeof(text)-1,fp);
	}

/* Go through file */
while(!feof(fp) && (lines<23 || user==NULL)) {
	if (sock==-1) {
		lines++;  
		if (user->netlink->ver_major<=3 && user->netlink->ver_minor<2) 
			str=colour_com_strip(text2);
		else str=text2;
		if (str[strlen(str)-1]!='\n') 
			sprintf(text,"MSG %s\n%s\nEMSG\n",user->name,str);
		else sprintf(text,"MSG %s\n%sEMSG\n",user->name,str);
		write_sock(user->netlink->socket,text);
		num_chars+=strlen(str);
		fgets(text2,82,fp);
		continue;
		}
	str=text;

	/* Process line from file */
	while(*str) {
		if (*str=='\n') {
			if (buffpos>OUT_BUFF_SIZE-6) {
				write(sock,buff,buffpos);  buffpos=0;
				}
			/* Reset terminal before every newline */
			if (user!=0 && user->colour) {
				memcpy(buff+buffpos,"\033[0m",4);  buffpos+=4;
				}
			*(buff+buffpos)='\n';  *(buff+buffpos+1)='\r';  
			buffpos+=2;  ++str;
			}
		else {  
			/* Process colour commands in the file. See write_user()
			   function for full comments on this code. */
			if (*str=='/' && *(str+1)=='~') {  ++str;  continue;  }
			if (str!=text && *str=='~' && *(str-1)=='/') {
				*(buff+buffpos)=*str;  goto CONT;
				}
			if (*str=='~') {
				if (buffpos>OUT_BUFF_SIZE-6) {
					write(sock,buff,buffpos);  buffpos=0;
					}
				++str;
				for(i=0;i<NUM_COLS;++i) {
					if (!strncmp(str,colcom[i],2)) {
						if (user!=NULL && user->colour) {
							memcpy(buffpos+buff,colcode[i],strlen(colcode[i]));
							buffpos+=strlen(colcode[i])-1;
							}
						else buffpos--;
						++str;
						goto CONT;
						}
					}
				*(buff+buffpos)=*(--str);
				}
			else *(buff+buffpos)=*str;
			CONT:
			++buffpos;   ++str;
			}
		if (buffpos==OUT_BUFF_SIZE) {
			write(sock,buff,OUT_BUFF_SIZE);  buffpos=0;
			}
		}
	len=strlen(text);
	num_chars+=len;
	lines+=len/80+(len<80);
	fgets(text,sizeof(text)-1,fp);
	}
if (buffpos && sock!=-1) write(sock,buff,buffpos);

/* if user is logging on dont page file */
if (user==NULL) {  fclose(fp);  return 2;  };
if (feof(fp)) {
	user->filepos=0;  no_prompt=0;  retval=2;
	}
else  {
	/* store file position and file name */
	user->filepos+=num_chars;
	strcpy(user->page_file,filename);
	/* We use E here instead of Q because when on a remote system and
	   in COMMAND mode the Q will be intercepted by the home system and 
	   quit the user */
	write_user(user,"           ~BB*** Press <return> to continue, 'e'<return> to exit ***");
	no_prompt=1;
	}
fclose(fp);
return retval;
}



/*** Set global vars. hours,minutes,seconds,date,day,month,year ***/
set_date_time()
{
struct tm *tm_struct; /* structure is defined in time.h */
time_t tm_num;

/* Set up the structure */
time(&tm_num);
tm_struct=localtime(&tm_num);

/* Get the values */
tday=tm_struct->tm_yday;
tyear=1900+tm_struct->tm_year; /* Will this work past the year 2000? Hmm... */
tmonth=tm_struct->tm_mon;
tmday=tm_struct->tm_mday;
twday=tm_struct->tm_wday;
thour=tm_struct->tm_hour;
tmin=tm_struct->tm_min;
tsec=tm_struct->tm_sec; 
}



/*** Return pos. of second word in inpstr ***/
char *remove_first(inpstr)
char *inpstr;
{
char *pos=inpstr;
while(*pos<33 && *pos) ++pos;
while(*pos>32) ++pos;
while(*pos<33 && *pos) ++pos;
return pos;
}


/*** Get user struct pointer from name ***/
UR_OBJECT get_user(name)
char *name;
{
UR_OBJECT u;

name[0]=toupper(name[0]);
/* Search for exact name */
for(u=user_first;u!=NULL;u=u->next) {
	if (u->login || u->type==CLONE_TYPE) continue;
	if (!strcmp(u->name,name))  return u;
	}
/* Search for close match name */
for(u=user_first;u!=NULL;u=u->next) {
	if (u->login || u->type==CLONE_TYPE) continue;
	if (strstr(u->name,name))  return u;
	}
return NULL;
}


/*** Get room struct pointer from abbreviated name ***/
RM_OBJECT get_room(name)
char *name;
{
RM_OBJECT rm;

for(rm=room_first;rm!=NULL;rm=rm->next)
     if (!strncmp(rm->name,name,strlen(name))) return rm;
return NULL;
}


/*** Return level value based on level name ***/
get_level(name)
char *name;
{
int i;

i=0;
while(level_name[i][0]!='*') {
	if (!strcmp(level_name[i],name)) return i;
	++i;
	}
return -1;
}


/*** See if a user has access to a room. If room is fixed to private then
	it is considered a wizroom so grant permission to any user of WIZ and
	above for those. ***/
has_room_access(user,rm)
UR_OBJECT user;
RM_OBJECT rm;
{
if ((rm->access & PRIVATE) 
    && user->level<gatecrash_level 
    && user->invite_room!=rm
    && !((rm->access & FIXED) && user->level>=WIZ)) return 0;
return 1;
}


/*** See if user has unread mail, mail file has last read time on its 
     first line ***/
has_unread_mail(user)
UR_OBJECT user;
{
FILE *fp;
int tm;
char filename[80];

sprintf(filename,"%s/%s.M",USERFILES,user->name);
if (!(fp=fopen(filename,"r"))) return 0;
fscanf(fp,"%d",&tm);
fclose(fp);
if (tm>(int)user->read_mail) return 1;
return 0;
}

/*** This is function that sends mail to other users ***/
send_mail(user,to,ptr)
UR_OBJECT user;
char *to,*ptr;
{
NL_OBJECT nl;
FILE *infp,*outfp;
char *c,d,*service,filename[80],line[DNL+1];

/* See if remote mail */
c=to;  service=NULL;
while(*c) {
	if (*c=='@') {  
		service=c+1;  *c='\0'; 
		for(nl=nl_first;nl!=NULL;nl=nl->next) {
			if (!strcmp(nl->service,service) && nl->stage==UP) {
				send_external_mail(nl,user,to,ptr);
				return;
				}
			}
		sprintf(text,"Service %s unavailable.\n",service);
		write_user(user,text); 
		return;
		}
	++c;
	}

/* Local mail */
if (!(outfp=fopen("tempfile","w"))) {
	write_user(user,"Error in mail delivery.\n");
	write_syslog("ERROR: Couldn't open tempfile in send_mail().\n",0);
	return;
	}
/* Write current time on first line of tempfile */
fprintf(outfp,"%d\r",(int)time(0));

/* Copy current mail file into tempfile if it exists */
sprintf(filename,"%s/%s.M",USERFILES,to);
if (infp=fopen(filename,"r")) {
	/* Discard first line of mail file. */
	fgets(line,DNL,infp);

	/* Copy rest of file */
	d=getc(infp);  
	while(!feof(infp)) {  putc(d,outfp);  d=getc(infp);  }
	fclose(infp);
	}

/* Put new mail in tempfile */
if (user!=NULL) {
	if (user->type==REMOTE_TYPE)
		fprintf(outfp,"~OLFrom: %s@%s  %s\n",user->name,user->netlink->service,long_date(0));
	else fprintf(outfp,"~OLFrom: %s  %s\n",user->name,long_date(0));
	}
else fprintf(outfp,"~OLFrom: MAILER  %s\n",long_date(0));

fputs(ptr,outfp);
fputs("\n",outfp);
fclose(outfp);
rename("tempfile",filename);
write_user(user,"A disgruntled postal worker walks into the room and takes your mail.\n");
write_user(get_user(to),"\07~FT~OLA disgruntled postal worker walks into the room and hands you some mail.\n");
forward_email(to,user->name,ptr);
}

/*** Spool mail file and ask for confirmation of users existence on remote
	site ***/
send_external_mail(nl,user,to,ptr)
NL_OBJECT nl;
UR_OBJECT user;
char *to,*ptr;
{
FILE *fp;
char filename[80];

/* Write out to spool file first */
sprintf(filename,"%s/OUT_%s_%s@%s",MAILSPOOL,user->name,to,nl->service);
if (!(fp=fopen(filename,"a"))) {
	sprintf(text,"%s: unable to spool mail.\n",syserror);
	write_user(user,text);
	sprintf(text,"ERROR: Couldn't open file %s to append in send_external_mail().\n",filename);
	write_syslog(text,0);
	return;
	}
putc('\n',fp);
fputs(ptr,fp);
fclose(fp);

/* Ask for verification of users existence */
sprintf(text,"EXISTS? %s %s\n",to,user->name);
write_sock(nl->socket,text);

/* Rest of delivery process now up to netlink functions */
write_user(user,"A disgruntled postal worker walks into the room, takes your mail and walks off\nto the other talker to deliver it.\n"); 
}

/*** See if string contains any swearing ***/
contains_swearing(str)
char *str;
{
char *s;
int i;

if ((s=(char *)malloc(strlen(str)+1))==NULL) {
	write_syslog("ERROR: Failed to allocate memory in contains_swearing().\n",0);
	return 0;
	}
strcpy(s,str);
strtolower(s); 
i=0;
while(swear_words[i][0]!='*') {
	if (strstr(s,swear_words[i])) {  free(s);  return 1;  }
	++i;
	}
free(s);
return 0;
}


/*** Count the number of colour commands in a string ***/
colour_com_count(str)
char *str;
{
char *s;
int i,cnt;

s=str;  cnt=0;
while(*s) {
     if (*s=='~') {
          ++s;
          for(i=0;i<NUM_COLS;++i) {
               if (!strncmp(s,colcom[i],2)) {
                    cnt++;  s++;  continue;
                    }
               }
          continue;
          }
     ++s;
     }
return cnt;
}


/*** Strip out colour commands from string for when we are sending strings
     over a netlink to a talker that doesn't support them ***/
char *colour_com_strip(str)
char *str;
{
char *s,*t;
static char text2[ARR_SIZE];
int i;

s=str;  t=text2;
while(*s) {
	if (*s=='~') {
		++s;
		for(i=0;i<NUM_COLS;++i) {
			if (!strncmp(s,colcom[i],2)) {  s++;  goto CONT;  }
			}
		--s;  *t++=*s;
		}
	else *t++=*s;
	CONT:
	s++;
	}	
*t='\0';
return text2;
}


/*** Date string for board messages, mail, .who and .allclones ***/
char *long_date(which)
int which;
{
static char dstr[80];

if (which) sprintf(dstr,"on %s %d %s %d at %02d:%02d",day[twday],tmday,month[tmonth],tyear,thour,tmin);
else sprintf(dstr,"[ %s %d %s %d at %02d:%02d ]",day[twday],tmday,month[tmonth],tyear,thour,tmin);
return dstr;
}


/*** Clear the review buffer in the room ***/
clear_revbuff(rm)
RM_OBJECT rm;
{
int c;
for(c=0;c<REVIEW_LINES;++c) rm->revbuff[c][0]='\0';
rm->revline=0;
}


/*** Clear the screen ***/
cls(user)
UR_OBJECT user;
{
int i;

for(i=0;i<5;++i) write_user(user,"\n\n\n\n\n\n\n\n\n\n");		
}


/*** Convert string to upper case ***/
strtoupper(str)
char *str;
{
while(*str) {  *str=toupper(*str);  str++; }
}


/*** Convert string to lower case ***/
strtolower(str)
char *str;
{
while(*str) {  *str=tolower(*str);  str++; }
}


/*** Returns 1 if string is a positive number ***/
isnumber(str)
char *str;
{
while(*str) if (!isdigit(*str++)) return 0;
return 1;
}


/************ OBJECT FUNCTIONS ************/

/*** Construct user/clone object ***/
UR_OBJECT create_user()
{
UR_OBJECT user;
int i;

if ((user=(UR_OBJECT)malloc(sizeof(struct user_struct)))==NULL) {
	write_syslog("ERROR: Memory allocation failure in create_user().\n",0);
	return NULL;
	}

/* Append object into linked list. */
if (user_first==NULL) {  
	user_first=user;  user->prev=NULL;  
	}
else {  
	user_last->next=user;  user->prev=user_last;  
	}
user->next=NULL;
user_last=user;

/* initialise user structure */
user->type=USER_TYPE;
user->name[0]='\0';
user->desc[0]='\0';
user->in_phrase[0]='\0'; 
user->out_phrase[0]='\0';   
user->afk_mesg[0]='\0';
user->pass[0]='\0';
user->site[0]='\0';
user->arrested=0;
user->site_port=0;
user->last_site[0]='\0';
user->page_file[0]='\0';
user->mail_to[0]='\0';
user->inpstr_old[0]='\0';
user->buff[0]='\0';  
user->buffpos=0;
user->filepos=0;
user->read_mail=time(0);
user->room=NULL;
user->invite_room=NULL;
user->port=0;
user->login=0;
user->socket=-1;
user->attempts=0;
user->command_mode=0;
user->level=1;
user->vis=1;
user->ignall=0;
user->ignall_store=0;
user->ignshout=0;
user->ignchat=0;
if (user->level=JRWIZ) user->ignwiz=0; else user->ignwiz=1;
if (user->level=HONORED) user->ignhonor=0; else user->ignhonor=1;
if (user->level=ADMIN) user->ignadmin=0; else user->ignadmin=1;
if (user->level=SU) user->igngod=0; else user->igngod=1;
user->igntell=0;
user->muzzled=0;
user->remote_com=-1;
user->netlink=NULL;
user->pot_netlink=NULL; 
user->last_input=time(0);
user->last_login=time(0);
user->last_login_len=0;
user->total_login=0;
user->prompt=prompt_def;
user->colour=colour_def;
user->charmode_echo=charecho_def;
user->misc_op=0;
user->edit_op=0;
user->edit_line=0;
user->charcnt=0;
user->warned=0;
user->afk=0;
user->revline=0;
user->clone_hear=CLONE_HEAR_ALL;
user->malloc_start=NULL;
user->malloc_end=NULL;
user->owner=NULL;
for(i=0;i<REVTELL_LINES;++i) user->revbuff[i][0]='\0';
user->icq[0]='\0'; 
user->url[0]='\0'; 
user->age=0;
user->gender=0;
user->email[0]='\0'; 
user->hidemail=0;
user->autofwd=0;
user->twin=0;
user->tlose=0;
user->tdraw=0;
user->bank_balance=0;
user->bj_game=NULL;
user->editing=0;
return user;
}



/*** Destruct an object. ***/
destruct_user(user)
UR_OBJECT user;
{
/* Remove from linked list */
if (user==user_first) {
	user_first=user->next;
	if (user==user_last) user_last=NULL;
	else user_first->prev=NULL;
	}
else {
	user->prev->next=user->next;
	if (user==user_last) { 
		user_last=user->prev;  user_last->next=NULL; 
		}
	else user->next->prev=user->prev;
	}
free(user);
destructed=1;
}


/*** Construct room object ***/
RM_OBJECT create_room()
{
RM_OBJECT room;
int i;

if ((room=(RM_OBJECT)malloc(sizeof(struct room_struct)))==NULL) {
	fprintf(stderr,"NUTS: Memory allocation failure in create_room().\n");
	boot_exit(1);
	}
room->name[0]='\0';
room->label[0]='\0';
room->desc[0]='\0';
room->topic[0]='\0';
room->access=-1;
room->revline=0;
room->mesg_cnt=0;
room->inlink=0;
room->netlink=NULL;
room->netlink_name[0]='\0';
room->next=NULL;
for(i=0;i<MAX_LINKS;++i) {
	room->link_label[i][0]='\0';  room->link[i]=NULL;
	}
for(i=0;i<REVIEW_LINES;++i) room->revbuff[i][0]='\0';
if (room_first==NULL) room_first=room;
else room_last->next=room;
room_last=room;
return room;
}


/*** Construct link object ***/
NL_OBJECT create_netlink()
{
NL_OBJECT nl;

if ((nl=(NL_OBJECT)malloc(sizeof(struct netlink_struct)))==NULL) {
	sprintf(text,"NETLINK: Memory allocation failure in create_netlink().\n");
	write_syslog(text,1);
	return NULL;
	}
if (nl_first==NULL) { 
	nl_first=nl;  nl->prev=NULL;  nl->next=NULL;
	}
else {  
	nl_last->next=nl;  nl->next=NULL;  nl->prev=nl_last;
	}
nl_last=nl;

nl->service[0]='\0';
nl->site[0]='\0';
nl->verification[0]='\0';
nl->mail_to[0]='\0';
nl->mail_from[0]='\0';
nl->mailfile=NULL;
nl->buffer[0]='\0';
nl->ver_major=0;
nl->ver_minor=0;
nl->ver_patch=0;
nl->keepalive_cnt=0;
nl->last_recvd=0;
nl->port=0;
nl->socket=0;
nl->mesg_user=NULL;
nl->connect_room=NULL;
nl->type=UNCONNECTED;
nl->stage=DOWN;
nl->connected=0;
nl->lastcom=-1;
nl->allow=ALL;
nl->warned=0;
return nl;
}


/*** Destruct a netlink (usually a closed incoming one). ***/
destruct_netlink(nl)
NL_OBJECT nl;
{
if (nl!=nl_first) {
	nl->prev->next=nl->next;
	if (nl!=nl_last) nl->next->prev=nl->prev;
	else { nl_last=nl->prev; nl_last->next=NULL; }
	}
else {
	nl_first=nl->next;
	if (nl!=nl_last) nl_first->prev=NULL;
	else nl_last=NULL; 
	}
free(nl);
}


/*** Destroy all clones belonging to given user ***/
destroy_user_clones(user)
UR_OBJECT user;
{
UR_OBJECT u;

for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE && u->owner==user) {
		if (u->vis) sprintf(text,"The clone of %s disappears in a puff of smoke.\n",u->name);
		else sprintf(text,"The clone of Someone disappears in a puff of smoke.\n");
		write_room(u->room,text);
		destruct_user(u);
		}
	}
}


/************ NUTS PROTOCOL AND LINK MANAGEMENT FUNCTIONS ************/
/* Please don't alter these functions. If you do you may introduce 
   incompatabilities which may prevent other systems connecting or cause
   bugs on the remote site and yours. You may think it looks simple but
   even the newline count is important in some places. */

/*** Accept incoming server connection ***/
accept_server_connection(sock,acc_addr)
int sock;
struct sockaddr_in acc_addr;
{
NL_OBJECT nl,nl2,create_netlink();
RM_OBJECT rm;
char site[81];

/* Send server type id and version number */
sprintf(text,"NUTS %s\n",VERSION);
write_sock(sock,text);
strcpy(site,get_ip_address(acc_addr));
sprintf(text,"NETLINK: Received request connection from site %s.\n",site);
write_syslog(text,1);

/* See if legal site, ie site is in config sites list. */
for(nl2=nl_first;nl2!=NULL;nl2=nl2->next) 
	if (!strcmp(nl2->site,site)) goto OK;
write_sock(sock,"DENIED CONNECT 1\n");
close(sock);
write_syslog("NETLINK: Request denied, remote site not in valid sites list.\n",1);
return;

/* Find free room link */
OK:
for(rm=room_first;rm!=NULL;rm=rm->next) {
	if (rm->netlink==NULL && rm->inlink) {
		if ((nl=create_netlink())==NULL) {
			write_sock(sock,"DENIED CONNECT 2\n");  
			close(sock);  
			write_syslog("NETLINK: Request denied, unable to create netlink object.\n",1);
			return;
			}
		rm->netlink=nl;
		nl->socket=sock;
		nl->type=INCOMING;
		nl->stage=VERIFYING;
		nl->connect_room=rm;
		nl->allow=nl2->allow;
		nl->last_recvd=time(0);
		strcpy(nl->service,"<verifying>");
		strcpy(nl->site,site);
		write_sock(sock,"GRANTED CONNECT\n");
		write_syslog("NETLINK: Request granted.\n",1);
		return;
		}
	}
write_sock(sock,"DENIED CONNECT 3\n");
close(sock);
write_syslog("NETLINK: Request denied, no free room links.\n",1);
}
		

/*** Deal with netlink data on link nl ***/
exec_netcom(nl,inpstr)
NL_OBJECT nl;
char *inpstr;
{
int netcom_num,lev;
char w1[ARR_SIZE],w2[ARR_SIZE],w3[ARR_SIZE],*c,ctemp;

/* The most used commands have been truncated to save bandwidth, ie ACT is
   short for action, EMSG is short for end message. Commands that don't get
   used much ie VERIFICATION have been left long for readability. */
char *netcom[]={
"DISCONNECT","TRANS","REL","ACT","GRANTED",
"DENIED","MSG","EMSG","PRM","VERIFICATION",
"VERIFY","REMVD","ERROR","EXISTS?","EXISTS_NO",
"EXISTS_YES","MAIL","ENDMAIL","MAILERROR","KA",
"RSTAT","*"
};

/* The buffer is large (ARR_SIZE*2) but if a bug occurs with a remote system
   and no newlines are sent for some reason it may overflow and this will 
   probably cause a crash. Oh well, such is life. */
if (nl->buffer[0]) {
	strcat(nl->buffer,inpstr);  inpstr=nl->buffer;
	}
nl->last_recvd=time(0);

/* Go through data */
while(*inpstr) {
	w1[0]='\0';  w2[0]='\0';  w3[0]='\0';  lev=0;
	if (*inpstr!='\n') sscanf(inpstr,"%s %s %s %d",w1,w2,w3,&lev);
	/* Find first newline */
	c=inpstr;  ctemp=1; /* hopefully we'll never get char 1 in the string */
	while(*c) {
		if (*c=='\n') {  ctemp=*(c+1); *(c+1)='\0';  break; }
		++c;
		}
	/* If no newline then input is incomplete, store and return */
	if (ctemp==1) {  
		if (inpstr!=nl->buffer) strcpy(nl->buffer,inpstr);  
		return;  
		}
	/* Get command number */
	netcom_num=0;
	while(netcom[netcom_num][0]!='*') {
		if (!strcmp(netcom[netcom_num],w1))  break;
		netcom_num++;
		}
	/* Deal with initial connects */
	if (nl->stage==VERIFYING) {
		if (nl->type==OUTGOING) {
			if (strcmp(w1,"NUTS")) {
				sprintf(text,"NETLINK: Incorrect connect message from %s.\n",nl->service);
				write_syslog(text,1);
				shutdown_netlink(nl);
				return;
				}	
			/* Store remote version for compat checks */
			nl->stage=UP;
			w2[10]='\0'; 
			sscanf(w2,"%d.%d.%d",&nl->ver_major,&nl->ver_minor,&nl->ver_patch);
			goto NEXT_LINE;
			}
		else {
			/* Incoming */
			if (netcom_num!=9) {
				/* No verification, no connection */
				sprintf(text,"NETLINK: No verification sent by site %s.\n",nl->site);
				write_syslog(text,1);
				shutdown_netlink(nl);  
				return;
				}
			nl->stage=UP;
			}
		}
	/* If remote is currently sending a message relay it to user, don't
	   interpret it unless its EMSG or ERROR */
	if (nl->mesg_user!=NULL && netcom_num!=7 && netcom_num!=12) {
		/* If -1 then user logged off before end of mesg received */
		if (nl->mesg_user!=(UR_OBJECT)-1) write_user(nl->mesg_user,inpstr);   
		goto NEXT_LINE;
		}
	/* Same goes for mail except its ENDMAIL or ERROR */
	if (nl->mailfile!=NULL && netcom_num!=17) {
		fputs(inpstr,nl->mailfile);  goto NEXT_LINE;
		}
	nl->lastcom=netcom_num;
	switch(netcom_num) {
		case  0: 
		if (nl->stage==UP) {
			sprintf(text,"[ ~OLSYSTEM:~FY~RS Disconnecting from service %s in the %s. ]\n",nl->service,nl->connect_room->name);
			write_room(NULL,text);
			}
		shutdown_netlink(nl);  
		break;

		case  1: nl_transfer(nl,w2,w3,lev,inpstr);  break;
		case  2: nl_release(nl,w2);  break;
		case  3: nl_action(nl,w2,inpstr);  break;
		case  4: nl_granted(nl,w2);  break;
		case  5: nl_denied(nl,w2,inpstr);  break;
		case  6: nl_mesg(nl,w2); break;
		case  7: nl->mesg_user=NULL;  break;
		case  8: nl_prompt(nl,w2);  break;
		case  9: nl_verification(nl,w2,w3,0);  break;
		case 10: nl_verification(nl,w2,w3,1);  break;
		case 11: nl_removed(nl,w2);  break;
		case 12: nl_error(nl);  break;
		case 13: nl_checkexist(nl,w2,w3);  break;
		case 14: nl_user_notexist(nl,w2,w3);  break;
		case 15: nl_user_exist(nl,w2,w3);  break;
		case 16: nl_mail(nl,w2,w3);  break;
		case 17: nl_endmail(nl);  break;
		case 18: nl_mailerror(nl,w2,w3);  break;
		case 19: /* Keepalive signal, do nothing */ break;
		case 20: nl_rstat(nl,w2);  break;
		default: 
			sprintf(text,"NETLINK: Received unknown command '%s' from %s.\n",w1,nl->service);
			write_syslog(text,1);
			write_sock(nl->socket,"ERROR\n"); 
		}
	NEXT_LINE:
	/* See if link has closed */
	if (nl->type==UNCONNECTED) return;
	*(c+1)=ctemp;
	inpstr=c+1;
	}
nl->buffer[0]='\0';
}


/*** Deal with user being transfered over from remote site ***/
nl_transfer(nl,name,pass,lev,inpstr)
NL_OBJECT nl;
char *name,*pass,*inpstr;
int lev;
{
UR_OBJECT u,create_user();

/* link for outgoing users only */
if (nl->allow==OUT) {
	sprintf(text,"DENIED %s 4\n",name);
	write_sock(nl->socket,text);
	return;
	}
if (strlen(name)>USER_NAME_LEN) name[USER_NAME_LEN]='\0';

/* See if user is banned */
if (user_banned(name)) {
	if (nl->ver_major==3 && nl->ver_minor>=3 && nl->ver_patch>=3) 
		sprintf(text,"DENIED %s 9\n",name); /* new error for 3.3.3 */
	else sprintf(text,"DENIED %s 6\n",name); /* old error to old versions */
	write_sock(nl->socket,text);
	return;
	}

/* See if user already on here */
if (u=get_user(name)) {
	sprintf(text,"DENIED %s 5\n",name);
	write_sock(nl->socket,text);
	return;
	}

/* See if user of this name exists on this system by trying to load up
   datafile */
if ((u=create_user())==NULL) {		
	sprintf(text,"DENIED %s 6\n",name);
	write_sock(nl->socket,text);
	return;
	}
u->type=REMOTE_TYPE;
strcpy(u->name,name);
if (load_user_details(u)) {
	if (strcmp(u->pass,pass)) {
		/* Incorrect password sent */
		sprintf(text,"DENIED %s 7\n",name);
		write_sock(nl->socket,text);
		destruct_user(u);
		destructed=0;
		return;
		}
	}
else {
	/* Get the users description */
	if (nl->ver_major<=3 && nl->ver_minor<=3 && nl->ver_patch<1) 
		strcpy(text,remove_first(remove_first(remove_first(inpstr))));
	else strcpy(text,remove_first(remove_first(remove_first(remove_first(inpstr)))));
	text[USER_DESC_LEN]='\0';
	terminate(text);
	strcpy(u->desc,text);
	strcpy(u->in_phrase,"enters");
	strcpy(u->out_phrase,"goes");
	if (nl->ver_major==3 && nl->ver_minor>=3 && nl->ver_patch>=1) {
		if (lev>rem_user_maxlevel) u->level=rem_user_maxlevel;
		else u->level=lev; 
		}
	else u->level=rem_user_deflevel;
	}
/* See if users level is below minlogin level */
if (u->level<minlogin_level) {
	if (nl->ver_major==3 && nl->ver_minor>=3 && nl->ver_patch>=3) 
		sprintf(text,"DENIED %s 8\n",u->name); /* new error for 3.3.3 */
	else sprintf(text,"DENIED %s 6\n",u->name); /* old error to old versions */
	write_sock(nl->socket,text);
	destruct_user(u);
	destructed=0;
	return;
	}
strcpy(u->site,nl->service);
sprintf(text,"%s enters from cyberspace.\n",u->name);
write_room(nl->connect_room,text);
sprintf(text,"NETLINK: Remote user %s received from %s.\n",u->name,nl->service);
write_syslog(text,1);
u->room=nl->connect_room;
u->netlink=nl;
u->read_mail=time(0);
u->last_login=time(0);
num_of_users++;
sprintf(text,"GRANTED %s\n",name);
write_sock(nl->socket,text);
sprintf(text,"[ Remote user %s(%s) received from %s ]\n",u->name,levels[u->level],nl->service);
write_level(ARCH,1,text,NULL);
}
		

/*** User is leaving this system ***/
nl_release(nl,name)
NL_OBJECT nl;
char *name;
{
UR_OBJECT u;

if ((u=get_user(name))!=NULL && u->type==REMOTE_TYPE) {
	sprintf(text,"%s has left the talker.\n",u->name);
	write_room_except(u->room,text,u);
	sprintf(text,"NETLINK: Remote user %s released.\n",u->name);
	write_syslog(text,1);
	sprintf(text,"[ Remote user %s(%s) released ]\n",u->name,levels[u->level]);
	write_level(ARCH,1,text,NULL);
	destroy_user_clones(u);
	destruct_user(u);
	num_of_users--;
	return;
	}
sprintf(text,"NETLINK: Release requested for unknown/invalid user %s from %s.\n",name,nl->service);
write_syslog(text,1);
}


/*** Remote user performs an action on this system ***/
nl_action(nl,name,inpstr)
NL_OBJECT nl;
char *name,*inpstr;
{
UR_OBJECT u;
char *c,ctemp;

if (!(u=get_user(name))) {
	sprintf(text,"DENIED %s 8\n",name);
	write_sock(nl->socket,text);
	return;
	}
if (u->socket!=-1) {
	sprintf(text,"NETLINK: Action requested for local user %s from %s.\n",name,nl->service);
	write_syslog(text,1);
	return;
	}
inpstr=remove_first(remove_first(inpstr));
/* remove newline character */
c=inpstr; ctemp='\0';
while(*c) {
	if (*c=='\n') {  ctemp=*c;  *c='\0';  break;  }
	++c;
	}
u->last_input=time(0);
if (u->misc_op) {
	if (!strcmp(inpstr,"NL")) misc_ops(u,"\n");  
	else misc_ops(u,inpstr+4);
	return;
	}
if (u->afk) {
	write_user(u,"You are no longer AFK.\n");  
	if (u->vis) {
		sprintf(text,"%s has RETURNED!\n",u->name);
		write_room_except(u->room,text,u);
		}
	u->afk=0;
	}
word_count=wordfind(inpstr);
if (!strcmp(inpstr,"NL")) return; 
exec_com(u,inpstr);
if (ctemp) *c=ctemp;
if (!u->misc_op) prompt(u);
}


/*** Grant received from remote system ***/
nl_granted(nl,name)
NL_OBJECT nl;
char *name;
{
UR_OBJECT u;
RM_OBJECT old_room;

if (!strcmp(name,"CONNECT")) {
	sprintf(text,"NETLINK: Connection to %s granted.\n",nl->service);
	write_syslog(text,1);
	/* Send our verification and version number */
	sprintf(text,"VERIFICATION %s %s\n",verification,VERSION);
	write_sock(nl->socket,text);
	return;
	}
if (!(u=get_user(name))) {
	sprintf(text,"NETLINK: Grant received for unknown user %s from %s.\n",name,nl->service);
	write_syslog(text,1);
	return;
	}
/* This will probably occur if a user tried to go to the other site , got 
   lagged then changed his mind and went elsewhere. Don't worry about it. */
if (u->remote_com!=GO) {
	sprintf(text,"NETLINK: Unexpected grant for %s received from %s.\n",name,nl->service);
	write_syslog(text,1);
	return;
	}
/* User has been granted permission to move into remote talker */
write_user(u,"~FB~OLA strange feeling overwhelms you as you feel you are transported elsewhere... \n");
if (u->vis) {
	sprintf(text,"%s %s to the %s.\n",u->name,u->out_phrase,nl->service);
	write_room_except(u->room,text,u);
	}
else write_room_except(u->room,invisleave,u);
sprintf(text,"NETLINK: %s transfered to %s.\n",u->name,nl->service);
write_syslog(text,1);
old_room=u->room;
u->room=NULL; /* Means on remote talker */
u->netlink=nl;
u->pot_netlink=NULL;
u->remote_com=-1;
u->misc_op=0;  
u->filepos=0;  
u->page_file[0]='\0';
reset_access(old_room);
sprintf(text,"ACT %s look\n",u->name);
write_sock(nl->socket,text);
sprintf(text,"[ %s(%s) transferred to %s ]\n",u->name,levels[u->level],nl->service);
write_level(ARCH,1,text,NULL);
}


/*** Deny received from remote system ***/
nl_denied(nl,name,inpstr)
NL_OBJECT nl;
char *name,*inpstr;
{
UR_OBJECT u,create_user();
int errnum;
char *neterr[]={
"this site is not in the remote services valid sites list",
"the remote service is unable to create a link",
"the remote service has no free room links",
"the link is for incoming users only",
"a user with your name is already logged on the remote site",
"the remote service was unable to create a session for you",
"incorrect password. Use '.go <service> <remote password>'",
"your level there is below the remote services current minlogin level",
"you are banned from that service"
};

errnum=0;
sscanf(remove_first(remove_first(inpstr)),"%d",&errnum);
if (!strcmp(name,"CONNECT")) {
	sprintf(text,"NETLINK: Connection to %s denied, %s.\n",nl->service,neterr[errnum-1]);
	write_syslog(text,1);
	/* If wiz initiated connect let them know its failed */
	sprintf(text,"[ ~OLSYSTEM:~RS Connection to %s failed, %s. ]\n",nl->service,neterr[errnum-1]);
	write_level(com_level[CONN],1,text,NULL);
	close(nl->socket);
	nl->type=UNCONNECTED;
	nl->stage=DOWN;
	return;
	}
/* Is for a user */
if (!(u=get_user(name))) {
	sprintf(text,"NETLINK: Deny for unknown user %s received from %s.\n",name,nl->service);
	write_syslog(text,1);
	return;
	}
sprintf(text,"NETLINK: Deny %d for user %s received from %s.\n",errnum,name,nl->service);
write_syslog(text,1);
sprintf(text,"Sorry, %s.\n",neterr[errnum-1]);
write_user(u,text);
prompt(u);
u->remote_com=-1;
u->pot_netlink=NULL;
}


/*** Text received to display to a user on here ***/
nl_mesg(nl,name)
NL_OBJECT nl;
char *name;
{
UR_OBJECT u;

if (!(u=get_user(name))) {
	sprintf(text,"NETLINK: Message received for unknown user %s from %s.\n",name,nl->service);
	write_syslog(text,1);
	nl->mesg_user=(UR_OBJECT)-1;
	return;
	}
nl->mesg_user=u;
}


/*** Remote system asking for prompt to be displayed ***/
nl_prompt(nl,name)
NL_OBJECT nl;
char *name;
{
UR_OBJECT u;

if (!(u=get_user(name))) {
	sprintf(text,"NETLINK: Prompt received for unknown user %s from %s.\n",name,nl->service);
	write_syslog(text,1);
	return;
	}
if (u->type==REMOTE_TYPE) {
	sprintf(text,"NETLINK: Prompt received for remote user %s from %s.\n",name,nl->service);
	write_syslog(text,1);
	return;
	}
prompt(u);
}


/*** Verification received from remote site ***/
nl_verification(nl,w2,w3,com)
NL_OBJECT nl;
char *w2,*w3;
int com;
{
NL_OBJECT nl2;

if (!com) {
	/* We're verifiying a remote site */
	if (!w2[0]) {
		shutdown_netlink(nl);  return;
		}
	for(nl2=nl_first;nl2!=NULL;nl2=nl2->next) {
		if (!strcmp(nl->site,nl2->site) && !strcmp(w2,nl2->verification)) {
			switch(nl->allow) {
				case IN : write_sock(nl->socket,"VERIFY OK IN\n");  break;
				case OUT: write_sock(nl->socket,"VERIFY OK OUT\n");  break;
				case ALL: write_sock(nl->socket,"VERIFY OK ALL\n"); 
				}
			strcpy(nl->service,nl2->service);

			/* Only 3.2.0 and above send version number with verification */
			sscanf(w3,"%d.%d.%d",&nl->ver_major,&nl->ver_minor,&nl->ver_patch);
			sprintf(text,"NETLINK: Connected to %s in the %s.\n",nl->service,nl->connect_room->name);
			write_syslog(text,1);
			sprintf(text,"[ ~OLSYSTEM:~RS New connection to service %s in the %s. ]\n",nl->service,nl->connect_room->name);
			write_room(NULL,text);
			return;
			}
		}
	write_sock(nl->socket,"VERIFY BAD\n");
	shutdown_netlink(nl);
	return;
	}
/* The remote site has verified us */
if (!strcmp(w2,"OK")) {
	/* Set link permissions */
	if (!strcmp(w3,"OUT")) {
		if (nl->allow==OUT) {
			sprintf(text,"NETLINK: WARNING - Permissions deadlock, both sides are outgoing only.\n");
			write_syslog(text,1);
			}
		else nl->allow=IN;
		}
	else {
		if (!strcmp(w3,"IN")) {
			if (nl->allow==IN) {
				sprintf(text,"NETLINK: WARNING - Permissions deadlock, both sides are incoming only.\n");
				write_syslog(text,1);
				}
			else nl->allow=OUT;
			}
		}
	sprintf(text,"NETLINK: Connection to %s verified.\n",nl->service);
	write_syslog(text,1);
	sprintf(text,"[ ~OLSYSTEM:~RS New connection to service %s in the %s. ]\n",nl->service,nl->connect_room);
	write_room(NULL,text);
	return;
	}
if (!strcmp(w2,"BAD")) {
	sprintf(text,"NETLINK: Connection to %s has bad verification.\n",nl->service);
	write_syslog(text,1);
	/* Let wizes know its failed , may be wiz initiated */
	sprintf(text,"[ ~OLSYSTEM:~RS Connection to %s failed, bad verification. ]\n",nl->service);
	write_level(com_level[CONN],1,text,NULL);
	shutdown_netlink(nl);  
	return;
	}
sprintf(text,"NETLINK: Unknown verify return code from %s.\n",nl->service);
write_syslog(text,1);
shutdown_netlink(nl);
}


/* Remote site only sends REMVD (removed) notification if user on remote site 
   tries to .go back to his home site or user is booted off. Home site doesn't
   bother sending reply since remote site will remove user no matter what. */
nl_removed(nl,name)
NL_OBJECT nl;
char *name;
{
UR_OBJECT u;

if (!(u=get_user(name))) {
	sprintf(text,"NETLINK: Removed notification for unknown user %s received from %s.\n",name,nl->service);
	write_syslog(text,1);
	return;
	}
if (u->room!=NULL) {
	sprintf(text,"NETLINK: Removed notification of local user %s received from %s.\n",name,nl->service);
	write_syslog(text,1);
	return;
	}
sprintf(text,"NETLINK: %s returned from %s.\n",u->name,u->netlink->service);
write_syslog(text,1);
u->room=u->netlink->connect_room;
u->netlink=NULL;
if (u->vis) {
	sprintf(text,"%s %s\n",u->name,u->in_phrase);
	write_room_except(u->room,text,u);
	}
else write_room_except(u->room,invisenter,u);
look(u);
prompt(u);
}


/*** Got an error back from site, deal with it ***/
nl_error(nl)
NL_OBJECT nl;
{
if (nl->mesg_user!=NULL) nl->mesg_user=NULL;
/* lastcom value may be misleading, the talker may have sent off a whole load
   of commands before it gets a response due to lag, any one of them could
   have caused the error */
sprintf(text,"NETLINK: Received ERROR from %s, lastcom = %d.\n",nl->service,nl->lastcom);
write_syslog(text,1);
}


/*** Does user exist? This is a question sent by a remote mailer to
     verifiy mail id's. ***/
nl_checkexist(nl,to,from)
NL_OBJECT nl;
char *to,*from;
{
FILE *fp;
char filename[80];

sprintf(filename,"%s/%s.D",USERFILES,to);
if (!(fp=fopen(filename,"r"))) {
	sprintf(text,"EXISTS_NO %s %s\n",to,from);
	write_sock(nl->socket,text);
	return;
	}
fclose(fp);
sprintf(text,"EXISTS_YES %s %s\n",to,from);
write_sock(nl->socket,text);
}


/*** Remote user doesnt exist ***/
nl_user_notexist(nl,to,from)
NL_OBJECT nl;
char *to,*from;
{
UR_OBJECT user;
char filename[80];
char text2[ARR_SIZE];

if ((user=get_user(from))!=NULL) {
	sprintf(text,"[ ~OLSYSTEM:~RS User %s does not exist at %s, your mail bounced. ]\n",to,nl->service);
	write_user(user,text);
	}
else {
	sprintf(text2,"There is no user named %s at %s, your mail bounced.\n",to,nl->service);
	send_mail(NULL,from,text2);
	}
sprintf(filename,"%s/OUT_%s_%s@%s",MAILSPOOL,from,to,nl->service);
unlink(filename);
}


/*** Remote users exists, send him some mail ***/
nl_user_exist(nl,to,from)
NL_OBJECT nl;
char *to,*from;
{
UR_OBJECT user;
FILE *fp;
char text2[ARR_SIZE],filename[80],line[82];

sprintf(filename,"%s/OUT_%s_%s@%s",MAILSPOOL,from,to,nl->service);
if (!(fp=fopen(filename,"r"))) {
	if ((user=get_user(from))!=NULL) {
		sprintf(text,"[ ~OLSYSTEM:~RS An error occured during mail delivery to %s@%s. ]\n",to,nl->service);
		write_user(user,text);
		}
	else {
		sprintf(text2,"An error occured during mail delivery to %s@%s.\n",to,nl->service);
		send_mail(NULL,from,text2);
		}
	return;
	}
sprintf(text,"MAIL %s %s\n",to,from);
write_sock(nl->socket,text);
fgets(line,80,fp);
while(!feof(fp)) {
	write_sock(nl->socket,line);
	fgets(line,80,fp);
	}
fclose(fp);
write_sock(nl->socket,"\nENDMAIL\n");
unlink(filename);
}


/*** Got some mail coming in ***/
nl_mail(nl,to,from)
NL_OBJECT nl;
char *to,*from;
{
char filename[80];

sprintf(text,"NETLINK: Mail received for %s from %s.\n",to,nl->service);
write_syslog(text,1);
sprintf(filename,"%s/IN_%s_%s@%s",MAILSPOOL,to,from,nl->service);
if (!(nl->mailfile=fopen(filename,"w"))) {
	sprintf(text,"ERROR: Couldn't open file %s to write in nl_mail().\n",filename);
	write_syslog(text,0);
	sprintf(text,"MAILERROR %s %s\n",to,from);
	write_sock(nl->socket,text);
	return;
	}
strcpy(nl->mail_to,to);
strcpy(nl->mail_from,from);
}


/*** End of mail message being sent from remote site ***/
nl_endmail(nl)
NL_OBJECT nl;
{
FILE *infp,*outfp;
char c,infile[80],mailfile[80],line[DNL+1];

fclose(nl->mailfile);
nl->mailfile=NULL;

sprintf(mailfile,"%s/IN_%s_%s@%s",MAILSPOOL,nl->mail_to,nl->mail_from,nl->service);

/* Copy to users mail file to a tempfile */
if (!(outfp=fopen("tempfile","w"))) {
	write_syslog("ERROR: Couldn't open tempfile in netlink_endmail().\n",0);
	sprintf(text,"MAILERROR %s %s\n",nl->mail_to,nl->mail_from);
	write_sock(nl->socket,text);
	goto END;
	}
fprintf(outfp,"%d\r",(int)time(0));

/* Copy old mail file to tempfile */
sprintf(infile,"%s/%s.M",USERFILES,nl->mail_to);
if (!(infp=fopen(infile,"r"))) goto SKIP;
fgets(line,DNL,infp);
c=getc(infp);
while(!feof(infp)) {  putc(c,outfp);  c=getc(infp);  }
fclose(infp);

/* Copy received file */
SKIP:
if (!(infp=fopen(mailfile,"r"))) {
	sprintf(text,"ERROR: Couldn't open file %s to read in netlink_endmail().\n",mailfile);
	write_syslog(text,0);
	sprintf(text,"MAILERROR %s %s\n",nl->mail_to,nl->mail_from);
	write_sock(nl->socket,text);
	goto END;
	}
fprintf(outfp,"~OLFrom: %s@%s  %s",nl->mail_from,nl->service,long_date(0));
c=getc(infp);
while(!feof(infp)) {  putc(c,outfp);  c=getc(infp);  }
fclose(infp);
fclose(outfp);
rename("tempfile",infile);
write_user(get_user(nl->mail_to),"\07~FT~OL~LIA disgruntled postal worker walks into the room and hands you some mail.\n");

END:
nl->mail_to[0]='\0';
nl->mail_from[0]='\0';
unlink(mailfile);
}


/*** An error occured at remote site ***/
nl_mailerror(nl,to,from)
NL_OBJECT nl;
char *to,*from;
{
UR_OBJECT user;

if ((user=get_user(from))!=NULL) {
	sprintf(text,"[ ~OLSYSTEM:~RS An error occured during mail delivery to %s@%s. ]\n",to,nl->service);
	write_user(user,text);
	}
else {
	sprintf(text,"An error occured during mail delivery to %s@%s.\n",to,nl->service);
	send_mail(NULL,from,text);
	}
}


/*** Send statistics of this server to requesting user on remote site ***/
nl_rstat(nl,to)
NL_OBJECT nl;
char *to;
{
char str[80];

gethostname(str,80);
if (nl->ver_major<=3 && nl->ver_minor<2)
	sprintf(text,"MSG %s\n\n*** Remote statistics ***\n\n",to);
else sprintf(text,"MSG %s\n\n~BB*** Remote statistics ***\n\n",to);
write_sock(nl->socket,text);
sprintf(text,"NUTS version         : %s\nHost                 : %s\n",VERSION,str);
write_sock(nl->socket,text);
sprintf(text,"Ports (Main/Wiz/Link): %d ,%d, %d\n",port[0],port[1],port[2]);
write_sock(nl->socket,text);
sprintf(text,"Number of users      : %d\nRemote user maxlevel : %s\n",num_of_users,level_name[rem_user_maxlevel]);
write_sock(nl->socket,text);
sprintf(text,"Remote user deflevel : %s\n\nEMSG\nPRM %s\n",level_name[rem_user_deflevel],to);
write_sock(nl->socket,text);
}


/*** Shutdown the netlink and pull any remote users back home ***/
shutdown_netlink(nl)
NL_OBJECT nl;
{
UR_OBJECT u;
char mailfile[80];

if (nl->type==UNCONNECTED) return;

/* See if any mail halfway through being sent */
if (nl->mail_to[0]) {
	sprintf(text,"MAILERROR %s %s\n",nl->mail_to,nl->mail_from);
	write_sock(nl->socket,text);
	fclose(nl->mailfile);
	sprintf(mailfile,"%s/IN_%s_%s@%s",MAILSPOOL,nl->mail_to,nl->mail_from,nl->service);
	unlink(mailfile);
	nl->mail_to[0]='\0';
	nl->mail_from[0]='\0';
	}
write_sock(nl->socket,"DISCONNECT\n");
close(nl->socket);  
for(u=user_first;u!=NULL;u=u->next) {
	if (u->pot_netlink==nl) {  u->remote_com=-1;  continue;  }
	if (u->netlink==nl) {
		if (u->room==NULL) {
			write_user(u,"~FB~OLYou feel yourself dragged back across the ether...\n");
			u->room=u->netlink->connect_room;
			u->netlink=NULL;
			if (u->vis) {
				sprintf(text,"%s %s\n",u->name,u->in_phrase);
				write_room_except(u->room,text,u);
				}
			else write_room_except(u->room,invisenter,u);
			look(u);  prompt(u);
			sprintf(text,"NETLINK: %s recovered from %s.\n",u->name,nl->service);
			write_syslog(text,1);
			continue;
			}
		if (u->type==REMOTE_TYPE) {
			sprintf(text,"%s vanishes!\n",u->name);
			write_room(u->room,text);
			destruct_user(u);
			num_of_users--;
			}
		}
	}
if (nl->stage==UP) 
	sprintf(text,"NETLINK: Disconnected from %s.\n",nl->service);
else sprintf(text,"NETLINK: Disconnected from site %s.\n",nl->site);
write_syslog(text,1);
if (nl->type==INCOMING) {
	nl->connect_room->netlink=NULL;
	destruct_netlink(nl);  
	return;
	}
nl->type=UNCONNECTED;
nl->stage=DOWN;
nl->warned=0;
}



/*************** START OF COMMAND FUNCTIONS AND THEIR SUBSIDS **************/

/*** Deal with user input ***/
exec_com(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
int i,len;
char filename[80],*comword=NULL;

com_num=-1;
if (word[0][0]=='.') comword=(word[0]+1);
else comword=word[0];
if (!comword[0]) {
	write_user(user,"Unknown command.\n");  return;
	}

/* get com_num */
if (!strcmp(word[0],">")) strcpy(word[0],"tell");
if (!strcmp(word[0],"<")) strcpy(word[0],"pemote");
if (!strcmp(word[0],"-")) strcpy(word[0],"echo");
if (!strcmp(word[0],"#")) strcpy(word[0],"shout");
if (!strcmp(word[0],"@")) strcpy(word[0],"who");
if (!strcmp(word[0],"'")) strcpy(word[0],"show");
if (inpstr[0]==';') strcpy(word[0],"emote");
	else inpstr=remove_first(inpstr);

i=0;
len=strlen(comword);
while(command[i][0]!='*') {
	if (!strncmp(command[i],comword,len)) {  com_num=i;  break;  }
	++i;
	}
if (user->room!=NULL && (com_num==-1 || com_level[com_num] > user->level)) {
	write_user(user,"Unknown command.\n");  return;
	}
/* See if user has gone across a netlink and if so then intercept certain 
   commands to be run on home site */
if (user->room==NULL) {
	switch(com_num) {
		case HOME: 
		case QUIT:
		case MODE:
		case PROMPT: 
		case COLOUR:
		case SUICIDE:
		case CHARECHO:
		case LEXAMINE:
		case LTELL:
		case LWHO:
		case LPEOPLE:
		case LBCAST:
		write_user(user,"~FY~OL*** Home execution ***\n");  break;

		default:
		sprintf(text,"ACT %s %s %s\n",user->name,word[0],inpstr);
		write_sock(user->netlink->socket,text);
		no_prompt=1;
		return;
		}
	}
/* Dont want certain commands executed by remote users */
if (user->type==REMOTE_TYPE) {
	switch(com_num) {
		case ARREST :
		case MUZZLE :
		case REBOOT :
		case SHUTDOWN :
		case PASSWD :
		case KILL :
		case DELETE :
		case BAN :
		case UNBAN :
		case PROMOTE :
		case DEMOTE :
		case ENTPRO :
		case CONN   :
		case DISCONN:
			write_user(user,"Sorry, remote users cannot use that command.\n");
			return;
		}
	}

/* Main switch */
if (user->arrested&&com_num!=RULES){write_user(user, "You can't do that while arrested.\n"); return;}
switch(com_num) {
	case QUIT: disconnect_user(user);  break;
	case LOOK: look(user);  break;
	case MODE: toggle_mode(user);  break;
	case SAY : 
		if (word_count<2) {
			write_user(user,"Say what?\n");  return;
			}
		say(user,inpstr);
		break;
	case SHOUT : shout(user,inpstr);  break;
	case TELL  : tell(user,inpstr);   break;
	case EMOTE : emote(user,inpstr);  break;
	case EMOTEALL: emoteall(user,inpstr); break;
	case PEMOTE: pemote(user,inpstr); break;
	case ECHO  : echo(user,inpstr);   break;
	case GO    : go(user);  break;
	case IGNALL: toggle_ignall(user);  break;
	case PROMPT: toggle_prompt(user);  break;
	case DESC  : set_desc(user,inpstr);  break;
	case INPHRASE : 
	case OUTPHRASE: 
		set_iophrase(user,inpstr);  break; 
	case PUBCOM :
	case PRIVCOM: set_room_access(user);  break;
	case LETMEIN: letmein(user);  break;
	case INVITE : invite(user);   break;
	case TOPIC  : set_topic(user,inpstr);  break;
	case MOVE   : move(user);  break;
	case BCAST  : bcast(user,inpstr);  break;
	case URENAME: user_rename_files(user,inpstr);  break;
	case WHO    : who(user,0);  break;
	case PEOPLE : who(user,1);  break;
	case HELP   : help(user);  break;
	case SHUTDOWN: shutdown_com(user);  break;
	case NEWS:
		sprintf(filename,"%s/%s",DATAFILES,NEWSFILE);
		switch(more(user,user->socket,filename)) {
			case 0: write_user(user,"There is no news.\n");  break;
			case 1: user->misc_op=2;
			}
		break;
	case READ  : read_board(user);  break;
	case WRITE : write_board(user,inpstr,0);  break;
	case WIPE  : wipe_board(user);  break;
	case SEARCH: search_boards(user);  break;
	case REVIEW: review(user);  break;
	case HOME  : home(user);  break;
	case STATUS: status(user);  break;
	case VER:
                sprintf(text,"Bolts %s by Nathan D Richards based on NUTS %s by Neil Robertson.\nThis copy licensed to: %s.\n",boltsversion,VERSION,talkerowner);
                write_user(user,text);  break;
	case RMAIL   : rmail(user);  break;
	case SMAIL   : smail(user,inpstr,0);  break;
	case DMAIL   : dmail(user);  break;
	case FROM    : mail_from(user);  break;
	case ENTPRO  : enter_profile(user,0);  break;
	case EXAMINE : examine(user);  break;
	case RMST    : rooms(user,1);  break;
	case RMSN    : rooms(user,0);  break;
	case NETSTAT : netstat(user);  break;
	case NETDATA : netdata(user);  break;
	case CONN    : connect_netlink(user);  break;
	case DISCONN : disconnect_netlink(user);  break;
	case PASSWD  : change_pass(user);  break;
	case KILL    : kill_user(user);  break;
	case PROMOTE : promote(user);  break;
	case DEMOTE  : demote(user);  break;
	case LISTBANS: listbans(user);  break;
	case BAN     : ban(user);  break;
	case UNBAN   : unban(user);  break;
	case VIS     : visibility(user,1);  break;
	case INVIS   : visibility(user,0);  break;
	case SITE    : site(user);  break;
	case WAKE    : wake(user);  break;
	case WIZ     : wiz(user,inpstr);  break;
	case MUZZLE  : muzzle(user);  break;
	case UNMUZZLE: unmuzzle(user);  break;
        case THINK   : think(user,inpstr); break; 
        case MUMBLE  : mumble(user,inpstr); break;
        case SING    : sing(user,inpstr); break;
        case WIZZES :
                sprintf(filename,"%s/%s",DATAFILES,WIZZESFILE);
                switch(more(user,user->socket,filename)) {
                        case 0: write_user(user,"There are no wizards.\n"); break;
                        case 1: user->misc_op=2;
                        }
                break;
        case RULES :                                                           
                sprintf(filename,"%s/%s",DATAFILES,RULEFILE);                  
                switch(more(user,user->socket,filename)) {                     
                        case 0: write_user(user,"There are no rules.\n");break;
                        case 1: user->misc_op=2;                               
                        }
		if (user->vis) sprintf(text,"%s(%s) has read the rules.\n",user->name,levels[user->level]);
		write_room_except(user->room,text,user);           
                break;
        case BEEP     : beep(user); break;
        case CPROG    : cprog(user); break;
	case WIZEMOTE : wizemote(user,inpstr); break;
	case IGNCHAT  : toggle_ignchat(user);  break;
	case IGNWIZ   : toggle_ignwiz(user);  break;
	case IGNHONOR : toggle_ignhonor(user);  break;
	case IGNADMIN   : toggle_ignadmin(user);  break;
	case IGNGOD   : toggle_igngod(user);  break;
	case JOIN : join(user,inpstr); break;
	case CHAT : chat(user,inpstr);  break;
	case CHATEMOTE : chatemote(user,inpstr);  break;
	case LEXAMINE : lexamine(user);  break;
	case LTELL : ltell(user,inpstr);  break;
	case LWHO : lwho(user);  break;
	case LPEOPLE : lpeople(user);  break;
	case LBCAST : lbcast(user,inpstr);  break;
	case ADMIN : admin(user,inpstr);  break;
	case ADMINEMOTE : adminemote(user,inpstr);  break;
	case EMERGENCY : emergency(user,inpstr);  break;
	case ACT : act(user,inpstr);  break;
	case ARREST : arrest(user); break;
	case UNARREST : unarrest(user); break;
	case HONOR : honor(user,inpstr);  break;
	case HONOREMOTE : honoremote(user,inpstr);  break;
	case GOD : god(user,inpstr);  break;
	case GODEMOTE : godemote(user,inpstr);  break;
	case ECHOALL : echoall(user,inpstr);  break;
	case ECHOTO : echoto(user,inpstr);  break;
	case STAFF : staff(user); break;
	case BRING : bring(user); break;
	case TIME : get_time(user); break;
	case ROULETTE : roulette(user); break;
        case SITECHANGE : sitechange(user,inpstr); break;
        case SAYTO : say_to(user,inpstr);  break;
        case SET : set(user,inpstr);  break;
	case SQUIT : squit(user); break;
	case FORCE : force(user,inpstr); break;
        case MAKEINVIS  : makeinvis(user); break;
	case MAKEVIS    : makevis(user); break;
	case FINGER : finger_host(user); break;
	case NSLOOKUP : nslookup(user); break;
	case CLSALL : cls_all(user); break;
	case CALENDAR : calendar(user); break;
	case FORTUNE : fortune(user); break;
	case CEMOTE : clone_emote(user,inpstr); break;
	case ICQPAGE : icqpage(user,inpstr); break;
	case TRACEROUTE : traceroute(user); break;
	case PING : ping(user); break;
        case TICTAC : tictac(user,inpstr); break;
	case HANGMAN : play_hangman(user); break;
	case GUESS : guess_hangman(user); break;
        case GIVECASH : givecash(user); break;
        case LENDCASH : lendcash(user); break;
        case BALANCE : balance(user); break;
	case GREET : greet(user,inpstr); break;
        case BJACK: play_blackjack(user);  break;
	case ALERT : alert(user); break;
	case MODIFY : modify(user); break;
	case SAVE : savedetails(user); break;
	case BBCAST : bbcast(user,inpstr); break;
	case FMAIL : forward_specific_mail(user); break;
	case SHOW : show(user,inpstr); break;
	case CTOPIC : clear_topic(user); break;
	case BFROM : board_from(user); break;
	case MUTTER : mutter(user,inpstr); break;
	case LISTAFKS : listafks(user); break;
	case PUZZLE : puzzle_com(user); break;
	case RANKS : ranks(user); break;
	case SHIFT : char_shift(user,inpstr); break;
	case SSHOUT : sshout(user,inpstr);  break;
        case RENAME: user_rename_files(user,inpstr);  break;
	case SETTIME : set_user_total_time(user); break;
	case SRBOOT  : do_srboot(); break;
	case MAP:
		sprintf(filename,"%s/%s",DATAFILES,MAPFILE);
		switch(more(user,user->socket,filename)) {
			case 0: write_user(user,"There is no map.\n");  break;
			case 1: user->misc_op=2;
			}
		break;
	case LOGGING  : logging(user); break;
	case SYSTEM   : system_details(user);  break;
	case CHARECHO : toggle_charecho(user);  break;
	case CLEARLINE: clearline(user);  break;
	case FIX      : change_room_fix(user,1);  break;
	case UNFIX    : change_room_fix(user,0);  break;
	case VIEWLOG  : viewlog(user);  break;
	case REVCLR   : revclr(user);  break;
	case CREATE   : create_clone(user);  break;
	case DESTROY  : destroy_clone(user);  break;
	case MYCLONES : myclones(user);  break;
	case ALLCLONES: allclones(user);  break;
	case SWITCH: clone_switch(user);  break;
	case CSAY  : clone_say(user,inpstr);  break;
	case CHEAR : clone_hear(user);  break;
	case RSTAT : remote_stat(user);  break;
	case SWBAN : swban(user);  break;
	case AFK   : afk(user,inpstr);  break;
	case CLS   : cls(user);  break;
	case COLOUR  : toggle_colour(user);  break;
	case IGNSHOUT: toggle_ignshout(user);  break;
	case IGNTELL : toggle_igntell(user);  break;
	case SUICIDE : suicide(user);  break;
	case DELETE  : delete_user(user,0);  break;
	case REBOOT  : reboot_com(user);  break;
	case RECOUNT : check_messages(user,2);  break;
	case REVTELL : revtell(user);  break;
	default: write_user(user,"Command not executed in exec_com().\n");
	}	
}



/*** Display details of room ***/
look(user)
UR_OBJECT user;
{
RM_OBJECT rm;
UR_OBJECT u;
char temp[81],null[1],*ptr;
char *afk="~BR(AFK)";
int i,exits,users;

rm=user->room;
if (rm->access & PRIVATE) sprintf(text,"\n~FTRoom: ~FR%s\n\n",rm->name);
else sprintf(text,"\n~FTRoom: ~FG%s\n\n",rm->name);
write_user(user,text);
write_user(user,user->room->desc);
exits=0;  null[0]='\0';
strcpy(text,"\n~FTExits are:");
for(i=0;i<MAX_LINKS;++i) {
	if (rm->link[i]==NULL) break;
	if (rm->link[i]->access & PRIVATE) 
		sprintf(temp,"  ~FR%s",rm->link[i]->name);
	else sprintf(temp,"  ~FG%s",rm->link[i]->name);
	strcat(text,temp);
	++exits;
	}
if (rm->netlink!=NULL && rm->netlink->stage==UP) {
	if (rm->netlink->allow==IN) sprintf(temp,"  ~FR%s*",rm->netlink->service);
	else sprintf(temp,"  ~FG%s*",rm->netlink->service);
	strcat(text,temp);
	}
else if (!exits) strcpy(text,"\n~FTThere are no exits.");
strcat(text,"\n\n");
write_user(user,text);

users=0;
for(u=user_first;u!=NULL;u=u->next) {
	if (u->room!=rm || u==user || (!u->vis && u->level>user->level)) 
		continue;
	if (u->vis) {
		if (!users++) write_user(user,"~FTYou can see:\n");
		if (u->afk) ptr=afk; else ptr=null;
		sprintf(text,"      %s %s~RS  %s\n",u->name,u->desc,ptr);
		write_user(user,text);
		}
	}
if (!users) write_user(user,"~FTYou are all alone here.\n");
write_user(user,"\n");

strcpy(text,"Access is ");
switch(rm->access) {
	case PUBLIC:  strcat(text,"set to ~FGpublic~RS");  break;
	case PRIVATE: strcat(text,"set to ~FRprivate~RS");  break;
	case FIXED_PUBLIC:  strcat(text,"~FRfixed~RS to ~FGpublic~RS");  break;
	case FIXED_PRIVATE: strcat(text,"~FRfixed~RS to ~FRprivate~RS");  break;
	}
sprintf(temp," and there are ~OL~FM%d~RS messages on the board.\n",rm->mesg_cnt);
strcat(text,temp);
write_user(user,text);
if (rm->topic[0]) {
	sprintf(text,"Current topic: %s\n",rm->topic);
	write_user(user,text);
	return;
	}
write_user(user,"Topic: ~RVNew here? Check out the commands - type .help\n");
}



/*** Switch between command and speech mode ***/
toggle_mode(user)
UR_OBJECT user;
{
if (user->command_mode) {
	write_user(user,"Now in speech mode.\n");
	user->command_mode=0;  return;
	}
write_user(user,"Now in command mode.\n");
user->command_mode=1;
}


/*** Shutdown the talker ***/
talker_shutdown(user,str,reboot)
UR_OBJECT user;
char *str;
int reboot;
{
UR_OBJECT u;
NL_OBJECT nl;
int i;
char *ptr;
char *args[]={ progname,confile,NULL };

if (user!=NULL) ptr=user->name; else ptr=str;
if (reboot) {
	write_room(NULL,"\07\n[ ~OLSYSTEM:~FY~LI Rebooting now!! ]\n\n");
	sprintf(text,"*** REBOOT initiated by %s ***\n",ptr);
	}
else {
	write_room(NULL,"\07\n[ ~OLSYSTEM:~FR~LI Shutting down now!! ]\n\n");
	sprintf(text,"*** SHUTDOWN initiated by %s ***\n",ptr);
	}
write_syslog(text,0);
for(nl=nl_first;nl!=NULL;nl=nl->next) shutdown_netlink(nl);
for(u=user_first;u!=NULL;u=u->next) disconnect_user(u);
for(i=0;i<3;++i) close(listen_sock[i]); 
if (reboot) {
	/* If someone has changed the binary or the config filename while this 
	   prog has been running this won't work */
	execvp(progname,args);
	/* If we get this far it hasn't worked */
	sprintf(text,"*** REBOOT FAILED %s: %s ***\n\n",long_date(1),sys_errlist[errno]);
	write_syslog(text,0);
	exit(12);
	}
sprintf(text,"*** SHUTDOWN complete %s ***\n\n",long_date(1));
write_syslog(text,0);
exit(0);
}


/*** Say user speech. ***/
say(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char type[10];

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot speak.\n");  return;
	}
if (user->room==NULL) {
	sprintf(text,"ACT %s say %s\n",user->name,inpstr);
	write_sock(user->netlink->socket,text);
	no_prompt=1;
	return;
	}
if (word_count<2 && user->command_mode) {
	write_user(user,"Say what?\n");  return;
	}
switch(inpstr[strlen(inpstr)-1]) {
     case '?': strcpy(type,"ask");  break;
     case '!': strcpy(type,"exclaim");  break;
     default : strcpy(type,"say");
     }
if (user->type==CLONE_TYPE) {
	sprintf(text,"Clone of %s %ss: %s\n",user->name,type,inpstr);
	write_room(user->room,text);
	record(user->room,text);
	return;
	}
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	}
sprintf(text,"You %s: %s\n",type,inpstr);
write_user(user,text);
sprintf(text,"%s %ss: %s\n",user->name,type,inpstr);
write_room_except(user->room,text,user);
record(user->room,text);
}


/*** Shout something ***/
shout(user,inpstr)
UR_OBJECT user;
char *inpstr;
{

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot shout.\n");  return;
	}
if (user->ignshout) {
	write_user(user,"You are currently ignoring shouts and shout emotes.\n");  return;
	}
if (word_count<2) {
	write_user(user,"Shout what?\n");  return;
	}
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	}
sprintf(text,"~OLYou shout:~RS %s\n",inpstr);
write_user(user,text);
sprintf(text,"~OL%s shouts:~RS %s\n",user->name,inpstr);
write_room_except(NULL,text,user);
}


/*** Tell another user something ***/
tell(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
UR_OBJECT u;
char type[5];

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot tell anyone anything.\n");  
	return;
	}
if (word_count<3) {
	write_user(user,"Tell who what?\n");  return;
	}
if (!(u=get_user(word[1]))) {
	write_user(user,notloggedon);  return;
	}
if (!u->vis) {                              
	write_user(user,notloggedon);
        return;                             
        }
if (u==user) {
	write_user(user,"Talking to yourself is the first sign of madness.\n");
	return;
	}
if (u->afk) {
	if (u->afk_mesg[0])
		sprintf(text,"%s is AFK, message is: %s\n",u->name,u->afk_mesg);
	else sprintf(text,"%s is AFK at the moment.\n",u->name);
	write_user(user,text);
	return;
	}
if (u->ignall && (user->level<JRWIZ || u->level>user->level)) {
	if (u->malloc_start!=NULL) 
		sprintf(text,"%s is using the editor at the moment.\n",u->name);
	else sprintf(text,"%s is ignoring everyone at the moment.\n",u->name);
	write_user(user,text);  
	return;
	}
if (u->igntell && (user->level<JRWIZ || u->level>user->level)) {
	sprintf(text,"%s is ignoring tells at the moment.\n",u->name);
	write_user(user,text);
	return;
	}
inpstr=remove_first(inpstr);
if (inpstr[strlen(inpstr)-1]=='?') strcpy(type,"ask");
else strcpy(type,"tell");
sprintf(text,"~OLYou %s %s:~RS %s\n",type,u->name,inpstr);
write_user(user,text);
if (u->room==NULL) sprintf(text,"~OL%s@%s %ss you:~RS %s\n",u->name,talkername,type,inpstr);
else sprintf(text,"~OL%s %ss you:~RS %s\n",user->name,type,inpstr);
write_user(u,text);
record_tell(u,text);
}


/*** Emote something ***/
emote(user,inpstr)
UR_OBJECT user;
char *inpstr;
{

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot emote.\n");  return;
	}
if (word_count<2 && inpstr[1]<33) {
	write_user(user,"Emote what?\n");  return;
	}
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	}
if (inpstr[0]==';') sprintf(text,"%s %s\n",user->name,inpstr+1);
else sprintf(text,"%s %s\n",user->name,inpstr);
write_room(user->room,text);
record(user->room,text);
}


/*** Do an emoteall ***/
emoteall(user,inpstr)
UR_OBJECT user;
char *inpstr;
{

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot emote.\n");  return;
	}
if (user->ignshout) {
	write_user(user,"You are currently ignoring shouts and shout emotes.\n");  return;
	}
if (word_count<2 && inpstr[1]<33) {
	write_user(user,"Shout emote what?\n");  return;
	}
if (inpstr[0]=='!') sprintf(text,"%s %s\n",user->name,inpstr+1);
else sprintf(text,"%s %s\n",user->name,inpstr);
write_room(NULL,text);
}


/*** Do a private emote ***/
pemote(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
UR_OBJECT u;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot emote.\n");  return;
	}
if (word_count<3) {
	write_user(user,"Private emote what?\n");  return;
	}
word[1][0]=toupper(word[1][0]);
if (!strcmp(word[1],user->name)) {
	write_user(user,"Emoting to yourself is the second sign of madness.\n");
	return;
	}
if (!(u=get_user(word[1]))) {
	write_user(user,notloggedon);  return;
	}
if (u->afk) {
	if (u->afk_mesg[0])
		sprintf(text,"%s is AFK, message is: %s\n",u->name,u->afk_mesg);
	else sprintf(text,"%s is AFK at the moment.\n",u->name);
	write_user(user,text);
	return;
	}
if (u->ignall && (user->level<JRWIZ || u->level>user->level)) {
	if (u->malloc_start!=NULL) 
		sprintf(text,"%s is using the editor at the moment.\n",u->name);
	else sprintf(text,"%s is ignoring everyone at the moment.\n",u->name);
	write_user(user,text);  return;
	}
if (u->igntell && (user->level<JRWIZ || u->level>user->level)) {
	sprintf(text,"%s is ignoring private emotes at the moment.\n",u->name);
	write_user(user,text);
	return;
	}
inpstr=remove_first(inpstr);
sprintf(text,"~OL(To %s)~RS %s %s\n",u->name,user->name,inpstr);
write_user(user,text);
if (u->room==NULL) sprintf(text,"~OL>>~RS %s@%s %s\n",user->name,talkername,inpstr);
else sprintf(text,"~OL>>~RS %s %s\n",user->name,inpstr);
write_user(u,text);
record_tell(u,text);
}


/*** Echo something to screen ***/
echo(user,inpstr)
UR_OBJECT user;
char *inpstr;
{

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot echo.\n");  return;
	}
if (word_count<2) {
	write_user(user,"Echo what?\n");  return;
	}
sprintf(text,"(%s) ",user->name);
write_level(JRWIZ,1,text,NULL);
sprintf(text,"%s\n",inpstr);
write_room(user->room,text);
record(user->room,text);
}



/*** Move to another room ***/
go(user)
UR_OBJECT user;
{
RM_OBJECT rm;
NL_OBJECT nl;
int i;

if (word_count<2) {
	write_user(user,"Go where?\n");  return;
	}
nl=user->room->netlink;
if (nl!=NULL && !strncmp(nl->service,word[1],strlen(word[1]))) {
	if (user->pot_netlink==nl) {
		write_user(user,"The remote service may be lagged, please be patient...\n");
		return;
		}
	rm=user->room;
	if (nl->stage<2) {
		write_user(user,"The netlink is inactive.\n");
		return;
		}
	if (nl->allow==IN && user->netlink!=nl) {
		/* Link for incoming users only */
		write_user(user,"Sorry, link is for incoming users only.\n");
		return;
		}
	/* If site is users home site then tell home system that we have removed
	   him. */
	if (user->netlink==nl) {
		write_user(user,"~FB~OLA strange feeling overwhelms you as you feel you are transported elsewhere...\n");
		sprintf(text,"REMVD %s\n",user->name);
		write_sock(nl->socket,text);
		if (user->vis) {
			sprintf(text,"%s goes to the %s\n",user->name,nl->service);
			write_room_except(rm,text,user);
			}
		else write_room_except(rm,invisleave,user);
		sprintf(text,"NETLINK: Remote user %s removed.\n",user->name);
		write_syslog(text,1);
		destroy_user_clones(user);
		destruct_user(user);
		reset_access(rm);
		num_of_users--;
		no_prompt=1;
		return;
		}
	/* Can't let remote user jump to yet another remote site because this will 
	   reset his user->netlink value and so we will lose his original link.
	   2 netlink pointers are needed in the user structure to allow this
	   but it means way too much rehacking of the code and I don't have the 
	   time or inclination to do it */
	if (user->type==REMOTE_TYPE) {
		write_user(user,"Sorry, due to software limitations you can only traverse one netlink.\n");
		return;
		}
	if (nl->ver_major<=3 && nl->ver_minor<=3 && nl->ver_patch<1) {
		if (!word[2][0]) 
			sprintf(text,"TRANS %s %s %s\n",user->name,user->pass,user->desc);
		else sprintf(text,"TRANS %s %s %s\n",user->name,(char *)crypt(word[2],"NU"),user->desc);
		}
	else {
		if (!word[2][0]) 
			sprintf(text,"TRANS %s %s %d %s\n",user->name,user->pass,user->level,user->desc);
		else sprintf(text,"TRANS %s %s %d %s\n",user->name,(char *)crypt(word[2],"NU"),user->level,user->desc);
		}	
	write_sock(nl->socket,text);
	user->remote_com=GO;
	user->pot_netlink=nl;  /* potential netlink */
	no_prompt=1;
	return;
	}
/* If someone tries to go somewhere else while waiting to go to a talker
   send the other site a release message */
if (user->remote_com==GO) {
	sprintf(text,"REL %s\n",user->name);
	write_sock(user->pot_netlink->socket,text);
	user->remote_com=-1;
	user->pot_netlink=NULL;
	}
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
if (rm==user->room) {
	sprintf(text,"You are already in the %s!\n",rm->name);
	write_user(user,text);
	return;
	}

/* See if link from current room */
for(i=0;i<MAX_LINKS;++i) {
	if (user->room->link[i]==rm) {
		move_user(user,rm,0);  return;
		}
	}
if (user->level<JRWIZ) {
	sprintf(text,"The %s is not adjoined to here.\n",rm->name);
	write_user(user,text);  
	return;
	}
move_user(user,rm,1);
}


/*** Called by go() and move() ***/
move_user(user,rm,teleport)
UR_OBJECT user;
RM_OBJECT rm;
int teleport;
{
RM_OBJECT old_room;

old_room=user->room;
if (teleport!=2 && !has_room_access(user,rm)) {
	write_user(user,"That room is currently private, you cannot enter.\n");  
	return;
	}
/* Reset invite room if in it */
if (user->invite_room==rm) user->invite_room=NULL;
if (!user->vis) {
	write_room(rm,invisenter);
	write_room_except(user->room,invisleave,user);
	goto SKIP;
	}
if (teleport==1) {
	sprintf(text,"~FT~OL%s appears out of the ground.\n",user->name);
	write_room(rm,text);
	sprintf(text,"~FT~OL%s disappears in a puff of smoke.\n",user->name);
	write_room_except(old_room,text,user);
	goto SKIP;
	}
if (teleport==2) {
	write_user(user,"\n~FT~OLYou have been magically transferred elsewhere...\n");
	sprintf(text,"~FT~OL%s appears out of the ground.\n",user->name);
	write_room(rm,text);
	if (old_room==NULL) {
		sprintf(text,"REL %s\n",user->name);
		write_sock(user->netlink->socket,text);
		user->netlink=NULL;
		}
	else {
		sprintf(text,"~FT~OL%s disappears in a puff of smoke.\n",user->name);
		write_room_except(old_room,text,user);
		}
	goto SKIP;
	}
sprintf(text,"%s %s.\n",user->name,user->in_phrase);
write_room(rm,text);
sprintf(text,"%s %s to the %s.\n",user->name,user->out_phrase,rm->name);
write_room_except(user->room,text,user);

SKIP:
user->room=rm;
look(user);
reset_access(old_room);
}


/*** Switch ignoring all on and off ***/
toggle_ignall(user)
UR_OBJECT user;
{
if (!user->ignall) {
	write_user(user,"You are now ignoring everyone.\n");
	sprintf(text,"%s is now ignoring everyone.\n",user->name);
	write_room_except(user->room,text,user);
	user->ignall=1;
	return;
	}
write_user(user,"You will now hear everyone again.\n");
sprintf(text,"%s is listening again.\n",user->name);
write_room_except(user->room,text,user);
user->ignall=0;
}


/*** Switch prompt on and off ***/
toggle_prompt(user)
UR_OBJECT user;
{
if (user->prompt) {
	write_user(user,"Prompt ~FRoff.\n");
	user->prompt=0;  return;
	}
write_user(user,"Prompt ~FGon.\n");
user->prompt=1;
}


/*** Set user description ***/
set_desc(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
if (word_count<2) {
	sprintf(text,"Your current description is: %s\n",user->desc);
	write_user(user,text);
	return;
	}
if (strstr(word[1],"(CLONE)")) {
	write_user(user,"You cannot have that description.\n");  return;
	}
if (strlen(inpstr)>USER_DESC_LEN) {
	write_user(user,"Description too long.\n");  return;
	}
strcpy(user->desc,inpstr);
write_user(user,"Description set.\n");
sprintf(text,"[ %s(%s) has changed %s description to: %s ]\n",user->name,levels[user->level],hisher[user->gender],inpstr);
write_level(ARCH,1,text,NULL);
}


/*** Set in and out phrases ***/
set_iophrase(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
if (strlen(inpstr)>PHRASE_LEN) {
	write_user(user,"Phrase too long.\n");  return;
	}
if (com_num==INPHRASE) {
	if (word_count<2) {
		sprintf(text,"Your current in phrase is: %s\n",user->in_phrase);
		write_user(user,text);
		return;
		}
	strcpy(user->in_phrase,inpstr);
	write_user(user,"In phrase set.\n");
	return;
	}
if (word_count<2) {
	sprintf(text,"Your current out phrase is: %s\n",user->out_phrase);
	write_user(user,text);
	return;
	}
strcpy(user->out_phrase,inpstr);
write_user(user,"Out phrase set.\n");
}


/*** Set rooms to public or private ***/
set_room_access(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;
int cnt;

rm=user->room;
if (word_count<2) rm=user->room;
else {
	if (user->level<gatecrash_level) {
		write_user(user,"You are not a high enough level to use the room option.\n");  
		return;
		}
	if ((rm=get_room(word[1]))==NULL) {
		write_user(user,nosuchroom);  return;
		}
	}
if (rm->access>PRIVATE) {
	if (rm==user->room) 
		write_user(user,"This room's access is fixed.\n"); 
	else write_user(user,"That room's access is fixed.\n");
	return;
	}
if (com_num==PUBCOM && rm->access==PUBLIC) {
	if (rm==user->room) 
		write_user(user,"This room is already public.\n");  
	else write_user(user,"That room is already public.\n"); 
	return;
	}
if (com_num==PRIVCOM) {
	if (rm->access==PRIVATE) {
		if (rm==user->room) 
			write_user(user,"This room is already private.\n");  
		else write_user(user,"That room is already private.\n"); 
		return;
		}
	cnt=0;
	for(u=user_first;u!=NULL;u=u->next) if (u->room==rm) ++cnt;
	if (cnt<min_private_users && user->level<ignore_mp_level) {
		sprintf(text,"You need at least %d users/clones in a room before it can be made private.\n",min_private_users);
		write_user(user,text);
		return;
		}
	write_user(user,"Room set to ~FRprivate.\n");
	if (rm==user->room) {
		sprintf(text,"%s has set the room to ~FRprivate.\n",u->name);
		write_room_except(rm,text,user);
		}
	else write_room(rm,"This room has been set to ~FRprivate.\n");
	rm->access=PRIVATE;
	return;
	}
write_user(user,"Room set to ~FGpublic.\n");
if (rm==user->room) {
	sprintf(text,"%s has set the room to ~FGpublic.\n",u->name);
	write_room_except(rm,text,user);
	}
else write_room(rm,"This room has been set to ~FGpublic.\n");
rm->access=PUBLIC;

/* Reset any invites into the room & clear review buffer */
for(u=user_first;u!=NULL;u=u->next) {
	if (u->invite_room==rm) u->invite_room=NULL;
	}
clear_revbuff(rm);
}


/*** Ask to be let into a private room ***/
letmein(user)
UR_OBJECT user;
{
RM_OBJECT rm;
int i;

if (word_count<2) {
	write_user(user,"Let you into where?\n");  return;
	}
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
if (rm==user->room) {
	sprintf(text,"You are already in the %s!\n",rm->name);
	write_user(user,text);
	return;
	}
for(i=0;i<MAX_LINKS;++i) 
	if (user->room->link[i]==rm) goto GOT_IT;
sprintf(text,"The %s is not adjoined to here.\n",rm->name);
write_user(user,text);  
return;

GOT_IT:
if (!(rm->access & PRIVATE)) {
	sprintf(text,"The %s is currently public.\n",rm->name);
	write_user(user,text);
	return;
	}
sprintf(text,"You ask politely to be let into the %s.\n",rm->name);
write_user(user,text);
sprintf(text,"%s asks politely to be let into the %s.\n",user->name,rm->name); 
write_room_except(user->room,text,user);
sprintf(text,"%s asks politely to be let in.\n",user->name);
write_room(rm,text);
}


/*** Invite a user into a private room ***/
invite(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;

if (word_count<2) {
	write_user(user,"Invite who?\n");  return;
	}
rm=user->room;
if (!(rm->access & PRIVATE)) {
	write_user(user,"This room is currently public.\n");
	return;
	}
if (!(u=get_user(word[1]))) {
	write_user(user,notloggedon);  return;
	}
if (u==user) {
	write_user(user,"Inviting yourself to somewhere is the third sign of madness.\n");
	return;
	}
if (u->room==rm) {
	sprintf(text,"%s is already here!\n",u->name);
	write_user(user,text);
	return;
	}
if (u->invite_room==rm) {
	sprintf(text,"%s has already been invited into here.\n",u->name);
	write_user(user,text);
	return;
	}
sprintf(text,"You invite %s in.\n",u->name);
write_user(user,text);
sprintf(text,"%s has invited you into the %s.\n",user->name,rm->name);
write_user(u,text);
u->invite_room=rm;
sprintf(text,"~OL~FR- - WARNING! - -~RS\n~OL%s has invited %s into the %s.~RS\n%s is now able to .review your conversation, even if %s doesn't come in.\nYou can cancel this by making the room .public, then .private again.\n~OL~FR- - - - - - - -\n",user->name,u->name,rm->name, capheshe[u->gender],u->name);
write_room_except(rm,text,user);
}

/*** Set the room topic ***/
set_topic(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
RM_OBJECT rm;

rm=user->room;
if (word_count<2) {
	if (!strlen(rm->topic)) {
		write_user(user,"No topic has been set yet.\n");  return;
		}
	sprintf(text,"The current topic is: %s\n",rm->topic);
	write_user(user,text);
	return;
	}
if (strlen(inpstr)>TOPIC_LEN) {
	write_user(user,"Topic too long.\n");  return;
	}
if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot set a topic.\n");  return;
	}
sprintf(text,"Topic set to: %s\n",inpstr);
write_user(user,text);
sprintf(text,"~BY%s has set the topic to:~RS %s\n",user->name,inpstr);
write_syslog(text,1);
write_room_except(rm,text,user);
sprintf(rm->topic,"~FT[%s %02d:%02d]~RS %s",user->name,thour,tmin,inpstr);
}

/*** Wizard moves a user to another room ***/
move(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;

if (word_count<2) {
	write_usage(user,"move <user> [<room>]\n");  return;
	}
if (!(u=get_user(word[1]))) {
	write_user(user,notloggedon);  return;
	}
if (word_count<3) rm=user->room;
else {
	if ((rm=get_room(word[2]))==NULL) {
		write_user(user,nosuchroom);  return;
		}
	}
if (user==u) {
	write_user(user,"Trying to move yourself this way is the fifteenth sign of madness.\n");  return;
	}
if (u->level>=user->level) {
	write_user(user,"You cannot move a user of equal or higher level than yourself.\n");
	return;
	}
if (!u->vis) {
	write_user(user,notloggedon);
	return;
	}
if (rm==u->room) {
	sprintf(text,"%s is already in the %s.\n",u->name,rm->name);
	write_user(user,text);
	return;
	};
if (u->room==NULL) {
	sprintf(text, "%s is currently in another world.\n",capheshe[u->gender]);
	write_user(user,text);
	return;
	}
if (!has_room_access(user,rm)) {
	sprintf(text,"The %s is currently private, %s cannot be moved there.\n",rm->name,u->name);
	write_user(user,text);  
	return;
	}
write_user(user,"~FT~OLYou mumble something...\n");
sprintf(text,"~FT~OL%s mumbles something...\n",user->name);
write_room_except(user->room,text,user);
move_user(u,rm,2);
prompt(u);
}


/*** Broadcast an important message ***/
bcast(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
if (word_count<2) {
	write_usage(user,"bcast <message>\n");  return;
	}
if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot broadcast anything.\n");  
	return;
	}
force_listen=1;
sprintf(text,"\07\n~BR*** Broadcast message from %s ***\n%s\n\n",user->name,inpstr);
write_room(NULL,text);  
}

/*** Sing something neat ***/
cprog(user)                  
UR_OBJECT user;              
{
UR_OBJECT u;                                                                   
                                                                               
if (word_count<2) {                                                            
        write_usage(user,"cprog <user>\n");  return;                     
        }                                                                      
if (user->muzzled) {                                                           
        write_user(user,"You are muzzled, you cannot cprog anyone.\n"); return;
        }                                                                      
if (!(u=get_user(word[1]))) {                                                  
        write_user(user,notloggedon);  return;                                 
        }                                                                      
if (!u->vis) {
        write_user(user,notloggedon);  return;                                 
        }                                                                      
if (u==user) {                                                                 
        write_user(user,"Trying to cprog yourself is the twelfth sign of madness.\n");
        return;                                                           
        }                                                                 
if (u->afk) {                                                             
        write_user(user,"You cannot cprog someone who is AFK.\n"); return;
        }
sprintf(text,"%s sings to you: Oh, I'm a C programmer and I'm okay,\n"
	        "I muck with indices and structs all day,\n"   
	        "And when it works, I shout hoo-ray,\n"        
	        "Oh, I'm a C programmer and I'm okay.\n",user->name);
write_user(u,text);                                                       
write_user(user,"Cprog sent.\n");                                         
}

/*** Beep someone ***/                                                          
beep(user)                                                                      
UR_OBJECT user;                                                                 
{                                                                               
UR_OBJECT u;                                                                    
                                                                                
if (word_count<2) {
        write_usage(user,"beep <user>\n");  return;                       
        }                                                                       
if (!u->vis) {
        write_user(user,notloggedon);  return;                                 
        }                                                                      
if (user->muzzled) {                                                            
        write_user(user,"You are muzzled, you cannot beep anyone.\n"); return; 
        }                                                                      
if (!(u=get_user(word[1]))) {                                                  
        write_user(user,notloggedon);  return;                                 
        }                                                                      
if (u==user) {                                                                 
        write_user(user,"Trying to beep yourself is the eleventh sign of madness.\n");
        return;                                                           
        }                                                                 
if (u->afk) {                                                             
        write_user(user,"You cannot beep someone who is AFK.\n");  return;
        }                                                                 
sprintf(text,"\07\n%s beeps you.\n\n",user->name);
write_user(u,text);
write_user(user,"Beep sent.\n");
}                                                                         

/*** Do a WIZ emote ***/
wizemote(user,inpstr)
UR_OBJECT user;
char *inpstr;
{

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot emote.\n");  return;
	}
if (user->ignwiz) {
	write_user(user,"You currently have the Wiz channel off.\n");  return;
	}
if (word_count<2) {
	write_user(user,"Wiz emote what?\n");  return;
	}
sprintf(text,"~OL[Wiz]~RS %s %s\n",user->name,inpstr);
write_room(NULL,text);
}

/***** Join *****/
join(user)
UR_OBJECT user;
{
UR_OBJECT u;
int i;

if (word_count<2) {
	write_user(user,"Join who?\n");  return;
	}
if ((u=get_user(word[1]))==NULL) {
        write_user(user,nosuchuser);  return;
        }
if (!u->vis) {                                                    
        write_user(user,nosuchuser);  return;
        }
if (u->room==NULL) {
	sprintf(text, "%s is currently in another world.\n",capheshe[u->gender]);
	write_user(user,text);
	return;
	}
if (u->room==user->room) {
	sprintf(text,"You are already with %s.\n",u->name);
	write_user(user,text);
	return;
	}
if (u->room->access==1&&user->invite_room!=u->room) {
	sprintf(text,"The %s is currently private.\n",u->room->name);
	write_user(user,text);
	return;
	}
if (((!strcmp(u->room->name, "wizroom")))&&user->level<JRWIZ) {
	sprintf(text, "%s cannot be joined in that room\n", u->name);
	write_user(user, text);
	return;
	}
if (u->vis) {                                                    
        sprintf(text,"~FG%s joins %s.~RS\n",user->name,u->name); 
        write_room_except(user->room,text,user);                 
        write_room(u->room,text);                                
        user->room=u->room;                                      
        look(user);                                              
        }                                                        
else write_user(user,"Join who?\n");                             
}

/*** chat channel ***/
chat(user,inpstr)
UR_OBJECT user;
char *inpstr;
{

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot chat.\n");  return;
	}
if (!strcmp(word[1],"on")) {                    
	if (user->ignchat) {
		write_user(user,"Chat channel on.\n");
		user->ignchat=0;
		return;
		}
	else {
		write_user(user,"You already have the Chat channel on.\n");
        	return;                                 
	        }
        }
if (!strcmp(word[1],"off")) {
	if (user->ignchat) {
		write_user(user,"You already have the Chat channel off.\n");
		return;
		}
	else {
		write_user(user,"Chat channel off.\n");
		user->ignchat=1;                       
		return;
		}
        }                                       
if (user->ignchat) {
	write_user(user,"You currently have the chat channel off.\n");  return;
	}
if (word_count<2) {
	write_user(user,"Chat what?\n");  return;
	}
sprintf(text,"~OL[Chat]~RS %s: %s\n",user->name,inpstr);
write_room(NULL,text);
}

/*** Do a chat emote ***/
chatemote(user,inpstr)
UR_OBJECT user;
char *inpstr;
{

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot emote.\n");  return;
	}
if (user->ignchat) {
	write_user(user,"You currently have the chat channel off.\n");  return;
	}
if (word_count<2) {
	write_user(user,"Chat emote what?\n");  return;
	}
sprintf(text,"~OL[Chat]~RS %s %s\n",user->name,inpstr);
write_room(NULL,text);
}

/*** local examine ***/
lexamine(user)
UR_OBJECT user;
{
if (user->room==NULL) { 
	examine(user);
	return;
	}
else write_user(user,"You can only issue this command over a netlink.\n");
}

/*** local tell ***/
ltell(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
if (user->room==NULL) { 
	tell(user,inpstr);
	return;
	}
else write_user(user,"You can only issue this command over a netlink.\n");
}

/*** local who ***/
lwho(user)
UR_OBJECT user;
{
if (user->room==NULL) { 
	who(user,0);
	return;
	}
else write_user(user,"You can only issue this command over a netlink.\n");
}

/*** local people ***/
lpeople(user)
UR_OBJECT user;
{
if (user->room==NULL) { 
	who(user,1);
	return;
	}
else write_user(user,"You can only issue this command over a netlink.\n");
}

/*** local bcast ***/
lbcast(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char *name;

if (user->room==NULL) { 
	bcast(user,inpstr);
	return;
	}
else write_user(user,"You can only issue this command over a netlink.\n");
}

/*** Admin Channel ***/
admin(user,inpstr)
UR_OBJECT user;
char *inpstr;
{

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot chat on the Honor channel.\n");  return;
	}
if (!strcmp(word[1],"on")) {                    
	if (user->ignadmin) {
		write_user(user,"Admin channel on.\n");
		user->ignadmin=0;
		return;
		}
	else {
		write_user(user,"You already have the Admin channel on.\n");
        	return;                                 
	        }
        }
if (!strcmp(word[1],"off")) {
	if (user->ignadmin) {
		write_user(user,"You already have the Admin channel off.\n");
		return;
		}
	else {
		write_user(user,"Admin channel off.\n");
                write_room(NULL,text);
		return;
		}
        }                                       
if (user->ignadmin) {
	write_user(user,"You currently have the Admin channel off.\n");  return;
	}
if (word_count<2) {
	write_user(user,"Admin what?\n");  return;
	}
sprintf(text,"~OL[Admin]~RS %s: %s\n",user->name,inpstr);
write_room(NULL,text);
}

/*** Do an Admin emote ***/
adminemote(user,inpstr)
UR_OBJECT user;
char *inpstr;
{

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot emote.\n");  return;
	}
if (user->ignadmin) {
	write_user(user,"You currently have the Admin channel off.\n");  return;
	}
if (word_count<2) {
	write_user(user,"Admin emote what?\n");  return;
	}
sprintf(text,"~OL[Admin]~RS %s %s\n",user->name,inpstr);
write_room(NULL,text);
}

/***** Emergency channel *****/
emergency(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char *name;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot use the Emergency channel.\n"); return;
	}
sprintf(text,"~OL(Emergency to all Wiz)~RS %s: %s\n",user->name,inpstr);
if (user->level<JRWIZ) { write_user(user,text);}
sprintf(text,"~OL(Emergency to all Wiz)~RS %s(%s): %s\n",user->name,levels[user->level],inpstr);
write_level(JRWIZ,1,text,NULL);
}

/***************** virus' version ***************/
/*** act ***/                                                              
 act(user,inpstr)                                                           
 UR_OBJECT user;                                                            
 char *inpstr;                                                              
 {                                                                          
 UR_OBJECT u;                                                               
 /* char type[5],*name; */
 int test=0;

if (word_count<2) {
	write_user(user,"Act what?\n");
	return;
}         
if (user->muzzled) {                                                           
	write_user(user,"You are muzzled, you cannot do that.\n");
	return;
}                                                                     

inpstr=remove_first(inpstr);

if (word_count>2) {
	if (!(u=get_user(word[2]))) {
		write_user(user, notloggedon);
		return;
	} 
	if (!u->vis) {                              
		write_user(user,"There is no one of that name logged on.\n");
		return;                             
	}
	if (u==user) {
		write_user(user, "Trying to .act yourself is the thirteenth sign of madness.\n");
		return;
	}
	if (u->room!=user->room && u->vis) {
		write_user(user, "They have to be in the same room as you.\n");
		return;
	}
	if (u->room!=user->room && !u->vis) {
		write_user(user, "There is no one of that name logged on.\n");
		return;
	}
	test=1;
	inpstr=remove_first(inpstr);
}     


if (!strncmp(word[1],"foo", strlen(word[1]))&& test) sprintf(text,"%s foos vaguely to %s.\n",user->name,u->name);
else if(!strncmp(word[1], "snowball", strlen(word[1]))&&test) sprintf(text, "%s throws a snowball at %s and lands a direct hit! SPLAT!\n", user->name, u->name);
else if (!strncmp(word[1], "smile",strlen(word[1]))&&test) sprintf(text, "%s smiles innocently to %s.\n", user->name, u->name);
else if (!strncmp(word[1], "growl",strlen(word[1]))&&test) sprintf(text, "%s growls at %s.\n", user->name, u->name);
else if (!strncmp(word[1], "fall",strlen(word[1]))) sprintf(text, "%s falls into a big pit and climbs back out again covered in slime.\n",user->name);
else if (!strncmp(word[1], "nod",strlen(word[1]))&&test) sprintf(text, "%s nods at %s.\n",user->name,u->name);
else if (!strncmp(word[1], "nog",strlen(word[1]))&&test) sprintf(text, "%s nogs at %s.\n",user->name,u->name);
else if (!strncmp(word[1], "heta",strlen(word[1]))&&test) sprintf(text, "%s screams HETA! right in %s's ear, loud enough to crack an eardrum.\n",user->name,u->name);
else if (!strncmp(word[1], "trout",strlen(word[1]))&&test) sprintf(text, "%s smacks %s around a bit with a large trout.\n",user->name,u->name);
else if (!strncmp(word[1], "cornholio",strlen(word[1]))) sprintf(text, "%s runs around with %s shirt over %s head and says 'I am Cornholio!  I need TP for my bunghole.'\n",user->name,hisher[user->gender],hisher[user->gender]);
else if (!strncmp(word[1], "buttmunch",strlen(word[1]))) sprintf(text, "%s says 'Hey Buttmunch, I'm gonna kick your ass!'\n",user->name);
else if (!strncmp(word[1], "fly",strlen(word[1]))) sprintf(text, "%s flies around the room.\n", user->name);
else if (!strncmp(word[1], "tickle",strlen(word[1]))&&test) sprintf(text, "%s tickles %s.\n%s giggles happily.\n",user->name,u->name,u->name);
else if (!strncmp(word[1], "hug",strlen(word[1]))&&test) sprintf(text, "%s hugs %s.\n",user->name,u->name);
else if (!strncmp(word[1], "snog",strlen(word[1]))&&test) sprintf(text, "%s snogs %s!\n",user->name,u->name);
else if (!strncmp(word[1], "welcome",strlen(word[1]))&&test) sprintf(text,"%s hands %s a womble and welcomes %s to %s.\nThe management suggests reading the ~OL.rules~RS and doing a ~OL.desc~RS.\nPlease read the ~OL.help~RS for a list of commands.\n",user->name,u->name,himher[u->gender],talkername);
else if (!strncmp(word[1], "grin",strlen(word[1]))&&test) sprintf(text,"%s grins at %s.\n",user->name,u->name);
else if (!strncmp(word[1], "wtf",strlen(word[1]))) sprintf(text,"%s wonders, 'What the fuck?!'\n",user->name);
else if (!strncmp(word[1], "sit",strlen(word[1]))) sprintf(text,"%s sits on the ceiling.\n",user->name);
else if (!strncmp(word[1], "doze",strlen(word[1]))) sprintf(text,"%s lies on the hammock with the sun on %s.\n",user->name,himher[user->gender]);
else if (!strncmp(word[1], "dive",strlen(word[1]))) sprintf(text,"%s dives into the water and goes for a swim.\n",user->name);
else if (!strncmp(word[1], "tackle",strlen(word[1]))&&test) sprintf(text,"%s tackles %s to the ground!\n",user->name,u->name);
else if (!strncmp(word[1], "lag",strlen(word[1]))) sprintf(text,"%s rips the heart out of lag and yells '~OLLAG BE GONE!!~RS'\n",user->name);
else if (!strncmp(word[1], "gump",strlen(word[1]))&&test) sprintf(text,"%s steals a box of chocolates from Forrest Gump and hands them to %s.\n",user->name,u->name);
else if (!strncmp(word[1], "wibble",strlen(word[1]))) sprintf(text,"%s sticks a pair of underpants on %s head.\n%s says: Wibble.\n",user->name,hisher[user->gender],user->name);
else if (!strncmp(word[1], "punch",strlen(word[1]))&&test) sprintf(text,"%s knocks the hell out of %s.\n%s falls to the ground crying like a baby.\n",user->name,u->name,u->name);
else if (!strncmp(word[1], "lick",strlen(word[1]))&&test) sprintf(text,"%s licks %s up and down like a lollipop...oooers!\n",user->name,u->name);
else if (!strncmp(word[1], "flirt",strlen(word[1]))&&test) sprintf(text,"%s flirts shamelessly with %s.\n",user->name,u->name);
else if (!strncmp(word[1], "smooch",strlen(word[1]))&&test) sprintf(text,"%s smooches %s lovingly.\n",user->name,u->name);
else if (!strncmp(word[1], "wave",strlen(word[1]))&&test) sprintf(text,"%s waves at %s.  Goodbye!\n",user->name,u->name);
else if (!strncmp(word[1], "kiss",strlen(word[1]))&&test) sprintf(text,"%s kisses %s with all the passion left in the world...ooers!\n",user->name,u->name);
else if (!strncmp(word[1], "wedgie",strlen(word[1]))&&test) sprintf(text, "%s rips %s's underoos giving %s a wedgie.\n",user->name,u->name,himher[u->gender]);
else if (!strncmp(word[1], "nibble",strlen(word[1]))&&test) sprintf(text, "%s nibbles playfully on %s's ear....oooers!\n",user->name,u->name);
else if (!strncmp(word[1], "trip",strlen(word[1]))) sprintf(text,"%s trips on a womble and falls on %s ass.\n",user->name,hisher[user->gender]);
else if (!strncmp(word[1], "bow",strlen(word[1]))) sprintf(text,"%s lifts %s hat and bows before you.\n",user->name,hisher[user->gender]);
else if (!strncmp(word[1], "rose",strlen(word[1]))&&test) sprintf(text,"%s offers %s a delicate red rose ~FR@}~FG--->---~RS.\n",user->name,u->name);
else if (!strncmp(word[1], "attack",strlen(word[1]))&&test) sprintf(text,"%s attacks %s, hugging and snogging.\n",user->name,u->name);
else if (!strncmp(word[1], "thwap",strlen(word[1]))&&test) sprintf(text,"%s thwaps you playfully.\n",user->name,u->name);
else if (!strncmp(word[1], "cuddle",strlen(word[1]))&&test) sprintf(text,"%s snuggles and cuddles %s lovingly.\n",user->name,u->name);
else if (!strncmp(word[1], "agree",strlen(word[1]))&&test) sprintf(text,"%s stands beside %s and says loudly 'I agree!'\n",user->name,u->name);
else if (!strncmp(word[1], "cheer",strlen(word[1]))&&test) sprintf(text,"%s cheers %s on with confetti and streamers. Yeah!\n",user->name,u->name);
else if (!strncmp(word[1], "giggle",strlen(word[1]))) sprintf(text,"%s giggles in the cutest manner.  Tee hee!\n",user->name);
else if (!strncmp(word[1], "caress",strlen(word[1]))&&test) sprintf(text,"%s caresses %s's cheek softly as %s looks into %s's eyes.\n",user->name,u->name,user->name,u->name);
else if (!strncmp(word[1], "purr",strlen(word[1]))&&test) sprintf(text,"%s sits in %s's lap and purrs. Purrrrr...\n",user->name,u->name);
else if (!strncmp(word[1], "sigh",strlen(word[1]))&&test) sprintf(text,"%s sighs gently making %s's heart melt. Awww...\n",user->name,u->name);
else if (!strncmp(word[1], "bewilder",strlen(word[1]))&&test) sprintf(text,"%s looks at %s in a confuzzled way.\n",user->name,u->name);
else if (!strncmp(word[1], "pout",strlen(word[1]))&&test) sprintf(text,"%s pouts softly, giving %s that puppydog look.\n",user->name,u->name);
else if (!strncmp(word[1], "cry",strlen(word[1]))&&test) sprintf(text,"%s looks at %s and bursts into tears.\n",user->name,u->name);
else if (!strncmp(word[1], "cackle",strlen(word[1]))) sprintf(text,"%s cackles evily.  Muhahaha!\n",user->name);
else if (!strncmp(word[1], "cringe",strlen(word[1]))&&test) sprintf(text,"%s looks at %s and steps back softly, cringing in terror.\n",user->name,u->name);
else if (!strncmp(word[1], "hifive",strlen(word[1]))&&test) sprintf(text,"%s hi5's %s with both hands. Alright!\n",user->name,u->name);
else if (!strncmp(word[1], "glare",strlen(word[1]))&&test) sprintf(text,"%s glares at %s icily which sends a cold chill down your spine.\n",user->name,u->name);
else if (!strncmp(word[1], "poke",strlen(word[1]))&&test) sprintf(text,"%s pokes %s in the belly, teehee.\n",user->name,u->name);
else if (!strncmp(word[1], "ponder",strlen(word[1]))) sprintf(text,"%s ponders 'Are you pondering what I'm pondering?'\n",user->name);
else if (!strncmp(word[1], "arse",strlen(word[1]))&&test) sprintf(text,"%s touches %s's arse and runs away giggling.\n",user->name,u->name);
else if (!strncmp(word[1], "moon",strlen(word[1]))&&test) sprintf(text,"%s pulls down %s drawers and moons %s while singing 'Blue Moon'.\n",user->name,hisher[user->gender],u->name);
else if (!strncmp(word[1], "wink",strlen(word[1]))&&test) sprintf(text,"%s winks suggestively at %s.\n",user->name,u->name);
else if (!strncmp(word[1], "smut",strlen(word[1]))&&test) sprintf(text,"%s jumps on %s and bounces up and down!  Woo Hoo!\n",user->name,u->name);
else if (!strncmp(word[1], "strip",strlen(word[1]))&&test) sprintf(text,"%s turns on some bump and grind and strips for %s!\n",user->name,u->name);
else if (!strncmp(word[1], "hide",strlen(word[1]))&&test) sprintf(text, "%s hides behind a fake plant, scared of %s.\n", user->name, u->name);
else if (!strncmp(word[1], "cackle",strlen(word[1]))) sprintf(text, "%s cackles insanely.\n",user->name);
else if (!strncmp(word[1], "lol",strlen(word[1]))) sprintf(text, "%s laughs out loud.\n",user->name);
else if (!strncmp(word[1], "rotfl",strlen(word[1]))) sprintf(text, "%s rolls on the floor laughing.\n",user->name);
else if (!strncmp(word[1], "bounce",strlen(word[1]))) sprintf(text, "%s bounces around the room.\n",user->name);
else if (!strncmp(word[1], "killlag",strlen(word[1]))) sprintf(text, "%s sticks a jagged spike in the back of the vicious lagmonster.\n",user->name);
else if (!strncmp(word[1], "eww",strlen(word[1]))) sprintf(text, "%s ewws, and holds %s nose.\n",user->name,hisher[user->gender]);
else if (!strncmp(word[1], "suckup",strlen(word[1]))&&test) sprintf(text, "%s declares %s unworthiness in %s's presence.\n", user->name, hisher[user->gender], u->name);
else if (!strncmp(word[1], "pounce",strlen(word[1]))&&test) sprintf(text, "%s playfully pounces on %s.\n", user->name, u->name);
else if (!strncmp(word[1], "tap",strlen(word[1]))&&test) sprintf(text, "%s taps %s on the shoulder.\n", user->name, u->name);
else if (!strncmp(word[1], "freak",strlen(word[1]))) sprintf(text, "%s is a FREAK!\n",user->name);
else if (!strncmp(word[1], "sniff",strlen(word[1]))) sprintf(text, "%s sniffs.\n",user->name);
else if (!strncmp(word[1], "chase",strlen(word[1]))&&test) sprintf(text, "%s chases %s around the room.\n", user->name, u->name);
else if (!strncmp(word[1], "taunt",strlen(word[1]))&&test) sprintf(text, "%s taunts %s.\n", user->name, u->name);
else if (!strncmp(word[1], "fume",strlen(word[1]))) sprintf(text, "%s fumes madly, smoke coming out of %s ears.\n",user->name,hisher[user->gender]);
else if (!strncmp(word[1], "grunt",strlen(word[1]))) sprintf(text, "%s grunts like Tim Allen. More power..arrr arrr arrr...\n",user->name);
else if (!strncmp(word[1], "slowdance",strlen(word[1]))&&test) sprintf(text, "%s slowdances with %s.\n", user->name, u->name);
else if (!strncmp(word[1], "hippy",strlen(word[1]))) sprintf(text, "%s screams 'GROOVY!!'\n",user->name);
else if (!strncmp(word[1], "annoy",strlen(word[1]))&&test) sprintf(text, "%s starts annoying %s.\n", user->name, u->name);
else if (!strncmp(word[1], "eek",strlen(word[1]))) sprintf(text, "%s says 'EEK!'\n",user->name);
else if (!strncmp(word[1], "doh",strlen(word[1]))) sprintf(text, "%s slaps %s forehead and loudly says 'DOH!!'\n",user->name,hisher[user->gender]);
else if (!strncmp(word[1], "date",strlen(word[1]))&&test) sprintf(text, "%s wants to go out with %s.\n", user->name, u->name);
else if (!strncmp(word[1], "smrt",strlen(word[1]))) sprintf(text, "%s is so smart!  S-M-R-T.\n",user->name);
else if (!strncmp(word[1], "drive",strlen(word[1]))&&test) sprintf(text, "%s pulls %s into a car and drives off.\n",user->name,u->name);
else if (!strncmp(word[1], "rescue",strlen(word[1]))&&test) sprintf(text, "%s bravely resuces %s.\n",user->name,u->name);
else if (!strncmp(word[1], "hyper",strlen(word[1]))) sprintf(text, "%s is getting really hyper.\n",user->name);
else if (!strncmp(word[1], "moron",strlen(word[1]))) sprintf(text, "%s acts like the moron that %s is.\n",user->name,heshe[user->gender]);
else if (!strncmp(word[1], "code",strlen(word[1]))) sprintf(text, "%s feels like coding a program.\n",user->name);
else if (!strncmp(word[1], "beg",strlen(word[1]))&&test) sprintf(text, "%s begs %s. PLLLEAASSE?!\n",user->name,u->name);
else if (!strncmp(word[1], "id",strlen(word[1]))&&test) sprintf(text, "%s says to %s: Alright, let's see some I.D. please.\n",user->name,u->name);
else if (!strncmp(word[1], "present",strlen(word[1]))&&test) sprintf(text, "%s wraps a present and gives it to %s.\n",user->name,u->name);
else if (!strncmp(word[1], "houston",strlen(word[1]))) sprintf(text, "%s reports: Houston... We have a problem.\n",user->name);
else if (!strncmp(word[1], "usa",strlen(word[1]))) sprintf(text, "%s chants: ~FRU~FWS~FBA~RS! ~FRU~FWS~FBA~RS! ~FRU~FWS~FBA~RS!\n",user->name);
else if (!strncmp(word[1], "beatup",strlen(word[1]))&&test) sprintf(text, "%s beats %s up, spraying blood and gore everywhere.\n",user->name,u->name);
else if (!strncmp(word[1], "peek",strlen(word[1]))&&test) sprintf(text, "%s peeks at %s when %s isn't looking.\n",user->name,u->name,heshe[u->gender]);
else if (!strncmp(word[1], "complain",strlen(word[1]))) sprintf(text, "%s complains. Aww, man!\n",user->name);
else if (!strncmp(word[1], "insult",strlen(word[1]))&&test) sprintf(text, "%s says something about %s's mama.\n",user->name,u->name);
else if (!strncmp(word[1], "plot",strlen(word[1]))) sprintf(text, "%s plots some evil scheme.\n",user->name);
else if (!strncmp(word[1], "cute",strlen(word[1]))&&test) sprintf(text, "%s says politely to %s, 'Ain't I just da' cutest thing?'\n",user->name,u->name);
else if (!strncmp(word[1], "slinky",strlen(word[1]))) sprintf(text, "%s rolls a slinky down the stairs. Hehehe. Everyone loves a slinky.\n",user->name);
else if (!strncmp(word[1], "mop",strlen(word[1]))) sprintf(text, "%s mops the floor.\n",user->name);
else if (!strncmp(word[1], "toldyaso",strlen(word[1]))&&test) sprintf(text, "%s says to %s: Told ya so!\n",user->name,u->name);
else if (!strncmp(word[1], "homework",strlen(word[1]))) sprintf(text, "%s sighs and gets back to %s homework.\n",user->name,hisher[user->gender]);
else if (!strncmp(word[1], "pathetic",strlen(word[1]))) sprintf(text, "%s is SO pathetic.\n",user->name);
else if (!strncmp(word[1], "champion",strlen(word[1]))) sprintf(text, "%s is the champion.  Muhahahaha.\n",user->name);
else if (!strncmp(word[1], "gulp",strlen(word[1]))) sprintf(text, "%s gulps.\n",user->name);
else if (!strncmp(word[1], "catch",strlen(word[1]))&&test) sprintf(text, "%s plays catch with %s.\n",user->name,u->name);
else if (!strncmp(word[1], "love",strlen(word[1]))&&test) sprintf(text, "%s loves %s very much.\n",user->name,u->name);
else if (!strncmp(word[1], "miss",strlen(word[1]))&&test) sprintf(text, "%s misses %s very much.\n",user->name,u->name);
else if (!strncmp(word[1], "visit",strlen(word[1]))&&test) sprintf(text, "%s comes over and visits %s.\n",user->name,u->name);
else if (!strncmp(word[1], "puppy",strlen(word[1]))&&test) sprintf(text, "%s gives %s a puppy. Aww, isn't that cute!\n",user->name,u->name);
else if (!strncmp(word[1], "yoyo",strlen(word[1]))) sprintf(text, "%s plays with a yoyo.\n",user->name);
else if (!strncmp(word[1], "html",strlen(word[1]))) sprintf(text, "%s feels like programming HTML.\n",user->name);
else if (!strncmp(word[1], "email",strlen(word[1]))&&test) sprintf(text, "%s sends %s some email...\n",user->name,u->name);
else if (!strncmp(word[1], "depressed",strlen(word[1]))) sprintf(text, "%s feels depressed today.\n",user->name);
else if (!strncmp(word[1], "flame",strlen(word[1]))&&test) sprintf(text, "%s flames %s.\n%s hides behind a fake plant, scared of %s.\n",user->name,u->name,u->name,user->name);
else if (!strncmp(word[1], "swoon",strlen(word[1]))&&test) sprintf(text, "%s swoons in %s's general direction.\n",user->name,u->name);
else if (!strncmp(word[1], "beammeup",strlen(word[1]))) sprintf(text, "%s taps %s communicator and says 'Beam me up.'\n%s disappears in a shimmering column of light.\n",user->name,hisher[user->gender],user->name);
else if (!strncmp(word[1], "read",strlen(word[1]))) sprintf(text, "%s reads a book.\n",user->name);
else if (!strncmp(word[1], "spam",strlen(word[1]))&&test) sprintf(text, "%s spams %s with a can of spam.\n%s passes %s a fork.\n",user->name,u->name,user->name,u->name);
else if (!strncmp(word[1], "woohoo",strlen(word[1]))) sprintf(text, "%s exclaims: ~OLW  O  O    H  O  O  !~RS\n",user->name);
else if (!strncmp(word[1], "type",strlen(word[1]))) sprintf(text, "%s types so fast, the keyboard is smoking.\n",user->name);
else if (!strncmp(word[1], "proud",strlen(word[1]))&&test) sprintf(text, "%s is proud of %s.\n",user->name,u->name);
else if (!strncmp(word[1], "envy",strlen(word[1]))&&test) sprintf(text, "%s envies %s.\n",user->name,u->name);
else if (!strncmp(word[1], "crash",strlen(word[1]))) sprintf(text, "%s tries to crash the talker and fails miserably.\n",user->name);
else if (!strncmp(word[1], "anykey",strlen(word[1]))) sprintf(text, "%s sees the 'Hit any key' prompt and hunts for the ~OLAny~RS key.\n",user->name);
else if (!strncmp(word[1], "cd",strlen(word[1]))) sprintf(text, "%s plays %s favourite CD.\n",user->name,hisher[user->gender]);
else if (!strncmp(word[1], "spock",strlen(word[1]))) sprintf(text, "%s says: That is not logical.\n",user->name);
else if (!strncmp(word[1], "eyebrow",strlen(word[1]))) sprintf(text, "%s raises an eyebrow.\n",user->name);
else if (!strncmp(word[1], "finger",strlen(word[1]))&&test) sprintf(text, "%s gives %s the finger.\n",user->name,u->name);
else if (!strncmp(word[1], "borg",strlen(word[1]))) sprintf(text, "%s says: ~OLI am %s of Borg.  You will be assimilated.  Resistance is futile.~RS\n",user->name,user->name);
else if (!strncmp(word[1], "radio",strlen(word[1]))) sprintf(text, "%s turns the radio on.\n",user->name);
else if (!strncmp(word[1], "scotty",strlen(word[1]))) sprintf(text, "%s summons Scotty and he says:  Cap'n, I canna give her any more power or she'll blow.\n",user->name);
else if (!strncmp(word[1], "bonk",strlen(word[1]))&&test) sprintf(text, "%s bonks %s on the head.  ~OLB O N K !~RS\n",user->name,u->name);
else if (!strncmp(word[1], "tag",strlen(word[1]))&&test) sprintf(text, "%s plays tag with %s...you're it!\n",user->name,u->name);
else if (!strncmp(word[1], "linux",strlen(word[1]))) sprintf(text, "%s says: Linux rules.\n",user->name);
else if (!strncmp(word[1], "bewith",strlen(word[1]))&&test) sprintf(text, "%s wishes they could be with %s.\n",user->name,u->name);
else if (!strncmp(word[1], "act",strlen(word[1]))) sprintf(text, "%s thinks there are way too many socials and someone must have too much time on their hands.\n",user->name);
else if (!strncmp(word[1], "cabal",strlen(word[1]))&&test) sprintf(text, "%s accuses %s of being a member of the cabal (we do not exist).\n",user->name,u->name);
else if (!strncmp(word[1], "accuse",strlen(word[1]))&&test) sprintf(text, "%s accuses %s of something.\n",user->name,u->name);
else if (!strncmp(word[1], "lollipop",strlen(word[1]))&&test) sprintf(text, "%s gives %s a lollipop.\n",user->name,u->name);
else if (!strncmp(word[1], "pepsi",strlen(word[1]))) sprintf(text, "%s gulps down a ~FBP~FRE~FBP~FRS~FBI~RS.\n",user->name);
else if (!strncmp(word[1], "coke",strlen(word[1]))) sprintf(text, "%s hates Coke.  Eww!\n",user->name);
else if (!strncmp(word[1], "grin",strlen(word[1]))) sprintf(text, "%s grins evilly.\n",user->name);
else if (!strncmp(word[1], "blarg",strlen(word[1]))) sprintf(text, "%s blargs strangely.\n",user->name);

 /* etc etc etc You can write thousands of acts here.*/
 else {
	write_user(user, "Available acts: foo, snowball, smile, growl, fall, nod, nog, heta,\n"
			"trout, cornholio, buttmunch, fly, tickle, hug, snog, welcome, grin, wtf, doze,\n"
			"sit, dive, tackle, lag, gump, wibble, punch, lick, flirt, smooch, wave, kiss,\n"
			"wedgie, nibble, trip, bow, rose, attack, thwap, cuddle, agree, cheer, giggle,\n"
			"caress, purr, sigh, bewilder, pout, cry, cackle, cringe, hifive, glare, poke,\n"
			"ponder, arse, moon, wink, smut, strip, hide, cackle, lol, rotfl, bounce,\n"
			"killlag, eww, suckup, pounce, tap, freak, sniff, chase, taunt, fume, grunt,\n"
			"slowdance, hippy, annoy, eek, doh, date, smrt, drive, rescue, hyper, moron, beg,\n"
			"code, id, present, mmmbop, houston, usa, beatup, but, peek, complain, insult,\n"
			"plot, cute, slinky, mop, toldyaso, homework, pathetic, champion, gulp, catch,\n"
			"love, miss, visit, puppy, yoyo, html, email, depressed, flame, swoon, beam,\n"
			"read, spam, woohoo, type, proud, envy, crash, anykey, cd, spock, eyebrow,\n"
			"finger, borg, radio, scotty, bonk, tag, linux, bewith, act, accuse, cabal,\n"
			"lollipop, pepsi, coke, grin, blarg.\n\nTotal of 145 acts.\n"); return;}
 write_room(user->room, text);
 return;
}

arrest(user)
UR_OBJECT user;
{
UR_OBJECT u;
UR_OBJECT uu;
if (word_count<2) {
	write_usage(user,"arrest <user>\n");  return;
	}
if ((u=get_user(word[1]))!=NULL) {
	if (u==user) {
		write_user(user,"Trying to arrest yourself is the seventeenth sign of madness.\n");
		return;
		}
	if (u->level>=user->level) {
		write_user(user,"You cannot arrest a user of equal or higher level than yourself.\n");
		return;
		}
	if (u->arrested>=user->level) {
		sprintf(text,"%s is already arrested.\n",u->name);
		write_user(user,text);  return;
		}
	sprintf(text,"%s has been arrested by level %s.\n",u->name,level_name[user->level]);
	write_user(user,text);
	write_user(u,"You have been arrested!\n");
	sprintf(text,"%s arrested %s.\n",user->name,u->name);
	write_syslog(text,1);
	sprintf(text,"[ %s(%s) has arrested %s. ]\n",user->name,levels[user->level],u->name);
	write_level(ARCH,1,text,NULL);

	move_user(u, jail, 4);
	u->arrested=user->level;
	return;
	}
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user session.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user session in arrest().\n",0);
	return;
	}
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->level>=user->level) {
	write_user(user,"You cannot arrest a user of equal or higher level than yourself.\n");
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->arrested>=user->level) {
	sprintf(text,"%s is already arrested.\n",u->name);
	write_user(user,text);
	destruct_user(u);
	destructed=0;
	return;
	}
u->socket=-2;
u->arrested=user->level;
strcpy(u->site,u->last_site);
save_user_details(u);
sprintf(text,"%s arrested by a level of %s.\n",u->name,level_name[user->level]); write_user(user,text);
send_mail(user,word[1],"You have been arrested!\n"); sprintf(text,"%s arrested %s.\n",user->name,u->name); write_syslog(text,1);
sprintf(text,"[ %s(%s) has arrested %s. ]\n",user->name,levels[user->level],u->name);
write_level(ARCH,1,text,NULL);
destruct_user(u); destructed=0;
}

unarrest(user)
UR_OBJECT user;
{
UR_OBJECT u;

if (word_count<2) {
	write_usage(user,"unarrest <user>\n");  return;
	}
if ((u=get_user(word[1]))!=NULL) {
	if (u==user) {
                write_user(user,"Trying to unarrest yourself is pretty funky.\n");
		return;
		}
	if (!u->arrested) {
		sprintf(text,"%s is not arrested.\n",u->name);  return;
		}
	if (u->arrested>user->level) {
		sprintf(text,"%ss arrest is set to level %s, you do not have the power to remove it.\n",u->name,level_name[u->arrested]);
		write_user(user,text);  return;
		}
	sprintf(text,"You unarrest %s.\n",u->name);
	write_user(user,text);
	write_user(u,"You have been unarrested!\n");
	sprintf(text,"%s unarrested %s.\n",user->name,u->name);
	write_syslog(text,1);
	sprintf(text,"[ %s(%s) has unarrested %s. ]\n",user->name,levels[user->level],u->name);
	write_level(ARCH,1,text,NULL);
	move_user(u, room_first, 2);
	u->arrested=0;
	return;
	}
/* User not logged on */
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user session.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user session in unarrest().\n",0);
	return;
	}
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->arrested>user->level) {
	sprintf(text,"%ss arrest is set to level %s, you do not have the power to remove it.\n",u->name,level_name[u->arrested]);
	write_user(user,text);
	destruct_user(u);
	destructed=0;
	return;
	}
u->socket=-2;
u->muzzled=0;
strcpy(u->site,u->last_site);
save_user_details(u);
sprintf(text,"You unarrest %s.\n",u->name);
write_user(user,text);
send_mail(user,word[1],"You have been unarrested.\n");
sprintf(text,"%s unarrested %s.\n",user->name,u->name);
write_syslog(text,1);
sprintf(text,"[ %s(%s) has unarrested %s. ]\n",user->name,levels[user->level],u->name);
write_level(ARCH,1,text,NULL);
destruct_user(u);
destructed=0;
}


/*** Honor Channel ***/
honor(user,inpstr)
UR_OBJECT user;
char *inpstr;
{

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot chat on the Honor channel.\n");  return;
	}
if (!strcmp(word[1],"on")) {                    
	if (user->ignhonor) {
		write_user(user,"Honor channel on.\n");
		user->ignhonor=0;
		return;
		}
	else {
		write_user(user,"You already have the Honor channel on.\n");
        	return;                                 
	        }
        }
if (!strcmp(word[1],"off")) {
	if (user->ignhonor) {
		write_user(user,"You already have the Honor channel off.\n");
		return;
		}
	else {
		write_user(user,"Honor channel off.\n");
		user->ignhonor=1; 
		return;
		}
        }                                       
if (user->ignhonor) {
	write_user(user,"You currently have the Honor channel off.\n");  return;
	}
if (word_count<2) {
	write_user(user,"Honor what?\n");  return;
	}
sprintf(text,"~OL[Honor]~RS %s: %s\n",user->name,inpstr);
write_room(NULL,text);
}

/*** Do an Honor emote ***/
honoremote(user,inpstr)
UR_OBJECT user;
char *inpstr;
{

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot emote.\n");  return;
	}
if (user->ignhonor) {
	write_user(user,"You currently have the Honor channel off.\n");  return;
	}
if (word_count<2) {
	write_user(user,"Honor emote what?\n");  return;
	}
sprintf(text,"~OL[Honor]~RS %s %s\n",user->name,inpstr);
write_room(NULL,text);
}

/*** God Channel ***/
god(user,inpstr)
UR_OBJECT user;
char *inpstr;
{

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot chat on the God channel.\n");  return;
	}
if (!strcmp(word[1],"on")) {                    
	if (user->igngod) {
		write_user(user,"God channel on.\n");
		user->igngod=0;
		return;
		}
	else {
		write_user(user,"You already have the God channel on.\n");
        	return;                                 
	        }
        }
if (!strcmp(word[1],"off")) {
	if (user->igngod) {
		write_user(user,"You already have the God channel off.\n");
		return;
		}
	else {
		write_user(user,"God channel off.\n");
		user->igngod=1; 
		return;
		}
        }                                       
if (user->igngod) {
	write_user(user,"You currently have the God channel off.\n");  return;
	}
if (word_count<2) {
	write_user(user,"God what?\n");  return;
	}
sprintf(text,"~OL[God]~RS %s: %s\n",user->name,inpstr);
write_room(NULL,text);
}

/*** Do a God emote ***/
godemote(user,inpstr)
UR_OBJECT user;
char *inpstr;
{

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot emote.\n");  return;
	}
if (user->igngod) {
	write_user(user,"You currently have the God channel off.\n");  return;
	}
if (word_count<2) {
	write_user(user,"God emote what?\n");  return;
	}
sprintf(text,"~OL[God]~RS %s %s\n",user->name,inpstr);
write_room(NULL,text);
}

/*** echoall ***/
echoall(user,inpstr)
UR_OBJECT user;
char *inpstr;
{

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot echoall.\n");  return;
	}
if (word_count<2) {
	write_usage(user,".echoall <message>\n");  return;
	}
sprintf(text,"(%s) ",user->name);
write_level(JRWIZ,1,text,NULL);
sprintf(text,"%s\n",inpstr);
write_room(NULL,text);
}

/*** Echo something to someone ***/
echoto(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
UR_OBJECT u;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot echo to anyone.\n");  return;
	}
if (word_count<2) {
	write_user(user,"Echo what to who?\n");  return;
	}
word[1][0]=toupper(word[1][0]);
if (!strcmp(word[1],user->name)) {
	write_user(user,"Echoing to yourself is the fourteenth sign of madness.\n");
	return;
	}
if (!(u=get_user(word[1]))) {
	write_user(user,notloggedon);  return;
	}
if (u->afk) {
	if (u->afk_mesg[0])
		sprintf(text,"%s is AFK, message is: %s\n",u->name,u->afk_mesg);
	else sprintf(text,"%s is AFK at the moment.\n",u->name);
	write_user(user,text);
	return;
	}
if (u->ignall && (user->level<JRWIZ || u->level>user->level)) {
	if (u->malloc_start!=NULL)
		sprintf(text,"%s is using the editor at the moment.\n",u->name);
	else sprintf(text,"%s is ignoring everyone at the moment.\n",u->name);
	write_user(user,text);  return;
	}
if (u->level>=JRWIZ) {
	inpstr=remove_first(inpstr);                              
	sprintf(text,"~OL(Echoto %s)~RS %s\n",u->name,inpstr);
	write_user(user,text);
	if (u->room==NULL) sprintf(text,"~OL>> (%s@%s)~RS %s\n",user->name,talkername,inpstr);
	else sprintf(text,"~OL>> (%s)~RS %s\n",user->name,inpstr);
	write_user(u,text);
	}
else {
	inpstr=remove_first(inpstr);                              
	sprintf(text,"~OL(Echoto %s)~RS %s\n",u->name,inpstr);
	write_user(user,text);
	sprintf(text,"~OL>>~RS %s\n",inpstr);
	write_user(u,text);
	}
}


/*********************** Uptime ************************/
uptime(user)
UR_OBJECT user;
{
int secs,days,hours,mins,num_clones;
char bstr[40];
strcpy(bstr,ctime(&boot_time));
secs=(int)(time(0)-boot_time);
days=secs/86400;
hours=(secs%86400)/3600;
mins=(secs%3600)/60;
secs=secs%60;
num_clones=0;
sprintf(text,"+=========================+ %s Uptime +=======================+\n",talkername);
write_user(user,text);
sprintf(text,"%s booted: %s",talkername,bstr);
write_user(user,text);
sprintf(text,"Uptime         : %d days, %d hours, %d minutes, %d seconds\n",days,hours,mins,secs);
write_user(user,text);
write_user(user,"+=======================================================================+\n");
return;
}

staff(user)
UR_OBJECT user;
{
UR_OBJECT u;
int i,lev;
i=0;
for (u=user_first; u!=NULL; u=u->next) {
        if (u->level<WIZARD || (!u->vis && u->level>user->level)) continue;
        if (u->level > user->level && u->vis);
        else lev=u->level;
        if (lev<WIZARD) continue;
        i++;
        if (i==1) {
                write_user(user,"\nWizards/Administrators Currently Online\n");
                write_user(user,"---------------------------------------\n");
                }
	if (user->level >= u->level) {
		if (u->afk)  sprintf(text,"    %-12s [~FR%-10s~RS] [ ~FG%s~RS ] : ~FR~OLAFK~RS: %s\n",u->name,level_name[u->level],u->room,u->afk_mesg);
		else sprintf(text,"    %-12s [~FR%-10s~RS] [ ~FG%s~RS ]\n",u->name,level_name[u->level],u->room);
	        }
	else {
		if (u->afk) sprintf(text,"    %-12s [~FR%-10s~RS] : ~FR~OLAFK~RS: %s\n",u->name,level_name[u->level],u->afk_mesg);
		else sprintf(text,"    %-12s [~FR%-10s~RS]\n",u->name,level_name[u->level]);
		}
        write_user(user,text);
        }
if (i) sprintf(text,"Total of %d wizards/administrators currently online.\n\n",i);
else sprintf(text,"Sorry, there are no administrators currently available.\n");
write_user(user,text);
}

fam_quote(user)
UR_OBJECT user;
{
	int num;
	char *format;
	num = rand() % quote_num;
	format=quotes[num];
	sprintf(text,format);
	write_user(user,"\n~FTQuote: ");
	write_user(user,text);
	return;
}

/*** Bring a user to to you. ***/
bring(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;

	rm=user->room;
	if (word_count<1) {
		write_usage(user,"bring <user>\n");  
		return;
	}
	if (!(u=get_user(word[1]))) {
		write_user(user,notloggedon);  
		return;
	}
	if (user==u) {
		write_user(user,"Trying to bring yourself this way is the fourth sign of madness.\n");  return;
	}
	if (u->level>=user->level){
		write_user(user,"You can't bring a user of higher or equal level then you.\n");
		return;
	}
	if (u->room==NULL) {
		sprintf(text, "%s is currently in another world.\n",capheshe[u->gender]);
		write_user(user,text);
		return;
	}
	if (rm==u->room) {
		sprintf(text,"%s is already in the %s.\n",u->name,rm->name);
		write_user(user,text);
		return;
	}
	write_user(user,"~FT~OLYou mumble something...\n");
	sprintf(text,"~FT~OL%s mumbles something...\n",user->name);
	write_room_except(user->room,text,user);
	move_user(u,rm,2);
	prompt(u);
}

/*** Get current system time ***/
get_time(user)
UR_OBJECT user;
{
char bstr[40],temp[80];
int secs,mins,hours,days;

/* Get some values */
strcpy(bstr,ctime(&boot_time));
bstr[strlen(bstr)-1]='\0';
secs=(int)(time(0)-boot_time);
days=secs/86400;
hours=(secs%86400)/3600;
mins=(secs%3600)/60;
secs=secs%60;
write_user(user,"+----------------------------------------------------------------------------+\n");
write_user(user,"| ~OL~FTTalker times~RS                                                               |\n");
write_user(user,"+----------------------------------------------------------------------------+\n");
sprintf(temp,"%s, %d %s, %02d:%02d:%02d %d",day[twday],tmday,month[tmonth],thour,tmin,tsec,tyear);
/* write_user(user,text); */
sprintf(text,"| The current system time is : ~OL%-45s~RS |\n",temp);
write_user(user,text);
sprintf(text,"| System booted              : ~OL%-45s~RS |\n",bstr);
write_user(user,text);
sprintf(temp,"%d days, %d hours, %d minutes, %d seconds",days,hours,mins,secs);
sprintf(text,"| Uptime                     : ~OL%-45s~RS |\n",temp);
write_user(user,text);
write_user(user,"+----------------------------------------------------------------------------+\n");
}

roulette(user)
UR_OBJECT user;
{
	int ran;
	
	write_user(user,"~FTYou decide to play Russian Roulette...~RS\n");
	write_user(user,"~FTYou hold your gun up to your head and pull the trigger...~RS\n");
	sprintf(text,"~FT%s decides to play Russian Roulette...~RS\n",user->name);
	write_room_except(user->room,text,user->name);
	sprintf(text,"~FT%s holds their gun to their head and pulls the trigger...~RS\n",user->name);
	write_room_except(user->room,text,user->name);
	
	ran=rand() % 5;
	if(ran==3) {
		write_user(user,"~FT~LI~OLBANG!!!~RS\n");
		sprintf(text,"~FT~LI~OL\07BANG\07~RS \n~FR%s falls dead!\n",user->name);
		write_room_except(user->room,text,user->name);

		write_user(user,"~FRYou played with fire and got burned!\n");
		write_room_except(user->room,"~FTThe wizzes rush in and clean up the mess...~RS\n",user->name);
		disconnect_user(user);
		return;
	}
	else {
		write_user(user,"~FT~LI~OL\07CLICK\07\n");
		sprintf(text,"~FT~LI~OL\07CLICK\07\n");
		write_room_except(user->room,text,user->name);
	}
	return;
}

/* Change your site to anything */
sitechange(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
  if (word_count<2) {
    sprintf(text,"~OL~FRYour current site is ( ~FG%s ~RS~OL~FR)",user->site);
    write_user(user,text);
    return;
    }
  strcpy(user->site,inpstr);
                                       
  sprintf(text,"~OL~FRYour site is now changed to ~FG[%s~RS~OL~FG] \n",inpstr);
  write_user(user,text);
                                       
  sprintf(text,"[ %s(%s) has changed %s site to: %s ]\n", user->name,levels[user->level],hisher[user->gender],inpstr);
  write_level(ARCH,1,text,NULL);
  }

/*** Direct a say to someone, even though the whole room can hear it ***/
say_to(user, inpstr)
char *inpstr;
UR_OBJECT user;
{
UR_OBJECT u;

if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot speak.\n");  return;
  }
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	}
if (word_count<3) {
  write_user(user,"Say what to who?\n");
  return;
  }
if (!(u=get_user(word[1]))) {
        write_user(user,notloggedon);  return;
        }
if (!strcmp(word[1],user->name)) {
	write_user(user,"Talking to yourself is the first sign of madness.\n");
	return;
	}
if (user->room==NULL) {
   sprintf(text,"ACT %s say %s\n",user->name,inpstr);
   write_sock(user->netlink->socket,text);
   no_prompt=1;
   return;
   }
inpstr=remove_first(inpstr);
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	}
sprintf(text,"You say (to %s): %s\n",u->name,inpstr);
write_user(user,text);
sprintf(text,"~FT%s says (to %s):~RS %s\n",user->name,u->name,inpstr);
write_room_except(user->room,text,user);
record(user->room,text);
}

 /*** Set - by Alan ***/
 set(user,inpstr)
 UR_OBJECT user;
{
 char *usage="Usage: set <age | autofwd |gender | email | hidemail | icq | url > <data>\n";

 if (word_count<3) {
         show_attributes(user); return;
         }
 if (!strcmp(word[1],"age")) {  age(user,inpstr);  return;  }
 if (!strcmp(word[1],"autofwd")) {  autofwd(user,inpstr);  return;  }
 if (!strcmp(word[1],"email")) {  email(user,inpstr);  return;  }
 if (!strcmp(word[1],"icq")) {  icq(user,inpstr);  return;  }
 if (!strcmp(word[1],"gender")) {  gender(user,inpstr);  return;  }
 if (!strcmp(word[1],"hidemail")) {  hidemail(user,inpstr);  return;  }
 if (!strcmp(word[1],"url")) {  url(user,inpstr);  return;  }
 show_attributes(user);
}
/*** END ***/

/*** SMARTgender by Poe and Kipper and Blink - Set Gender***/
gender(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
if (strlen(word[2])>1){
	sprintf(text,"No, no, gender may only be M, F or N\n");
	write_user(user,text);
	return;
	}

/* changed this -- Andy */
switch(inpstr[strlen(inpstr)-1]) {
case 'm':
case 'M':
	if (user->gender==1) {
        	sprintf(text,"Your gender is already %s! (%c)\n",genders[user->gender],genders[user->gender][0]);
        	write_user(user,text);
        	return;
        }
	user->gender=1;
	break;
case 'f':
case 'F':
        if (user->gender==2) {
                sprintf(text,"Your gender is already %s! (%c)\n",genders[user->gender],genders[user->gender][0]);
                write_user(user,text);
                return;
        }
        user->gender=2;
        break;
case 'n':
case 'N':
        if (!user->gender) {
                sprintf(text,"Your gender is already %s! (%c)\n",genders[user->gender],genders[user->gender][0]);
                write_user(user,text);
                return;
        }
        user->gender=0;
        break;
}
sprintf(text,"Your gender is now: %s (%c)\n",genders[user->gender],genders[user->gender][0]);
write_user(user,text);
save_user_details(user);
/*update userfile as soon as it is set*/
sprintf(text,"[ %s(%s) has changed %s gender to %s ]\n",user->name,levels[user->level],hisher[user->gender],genders[user->gender]);
write_level(ARCH,1,text,NULL);
return;
}

/*** ICQ by Alan ***/
icq(user,inpstr)
UR_OBJECT user;
int *inpstr;
{
if (strlen(word[2])>ICQ_LEN) {
  sprintf(text,"The maximum ICQ UIN length you can have is %d characters.\n",ICQ_LEN);
  write_user(user,text);
  return;
  }
strcpy(user->icq,word[2]);
/*** very plain code, but its just to test the other stuff first ***/
sprintf(text,"ICQ set to: %s\n",word[2]);
write_user(user,text);
/*** now you have to call the update command... so the change takes effect ***/
save_user_details(user,0);
sprintf(text,"[ %s(%s) has changed %s ICQ number to %s ]\n",user->name,levels[user->level],hisher[user->gender],word[2]);
write_level(ARCH,1,text,NULL);
return;
}
/*** END ***/

/*** URL ***/
url(user,inpstr)
UR_OBJECT user;
int *inpstr;
{
if (strlen(word[2])>URL_LEN) {
  sprintf(text,"The maximum URL length you can have is %d characters.\n",URL_LEN);
  write_user(user,text);
  return;
  }
strcpy(user->url,word[2]);
/*** very plain code, but its just to test the other stuff first ***/
sprintf(text,"URL set to: %s\n",word[2]);
write_user(user,text);
/*** now you have to call the update command... so the change takes effect ***/
save_user_details(user,0);
sprintf(text,"[ %s(%s) has changed %s URL to %s ]\n",user->name,levels[user->level],hisher[user->gender],word[2]);
write_level(ARCH,1,text,NULL);
return;
}
/*** END ***/

/*** age  ***/
age(user,inpstr)
UR_OBJECT user;
{
int temp;

if (strlen(word[2])==0) {
  write_usage(user,"age <your age>.\n");  return;
  }
if (strlen(word[2])>AGE_LEN) {
  sprintf(text,"Your age must be between 0 and 100.\n");
  write_user(user,text);
  return;
  }
if (!isnumber(word[2])) {
  sprintf(text,"That's not a number.\n");
  write_user(user,text);
  return;
  }
temp=atoi(word[2]);
user->age=temp;
/*** very plain code, but its just to test the other stuff first ***/
sprintf(text,"Age set to: %s\n",word[2]);
write_user(user,text);
/*** now you have to call the update command... so the change takes effect ***/
save_user_details(user);
sprintf(text,"[ %s(%s) has changed %s age to %s ]\n",user->name,levels[user->level],hisher[user->gender],word[2]);
write_level(ARCH,1,text,NULL);
return;
}
/*** END ***/

/*** E-Mail ***/
email(user,inpstr)
UR_OBJECT user;
int *inpstr;
{
if (strlen(word[2])>EMAIL_LEN) {
  sprintf(text,"Maximum of %d characters please..\n",EMAIL_LEN);
  write_user(user,text);
  return;
  }
if (!valid_email(user,word[2])) return;
else {
	strcpy(user->email,word[2]);
	/*** very plain code, but its just to test the other stuff first ***/
	sprintf(text,"E-mail address set to: %s\n",word[2]);
	write_user(user,text);
	/*** now you have to call the update command... so the change takes effect ***/
	save_user_details(user,0);
	sprintf(text,"[ %s(%s) has changed %s e-mail address to %s ]\n",user->name,levels[user->level],hisher[user->gender],word[2]);
	write_level(ARCH,1,text,NULL);
	return;
	}
}
/*** END ***/

int valid_email(user,address)
UR_OBJECT user;
char *address;
{
int e=0;

write_user(user,"\n~OL~FYChecking e-mail address...~RS\n");
if (!address) {
	write_user(user,"~OL~FRWARNING~FW: Nothing to check!~RS\n");
	return 0;
	}
if (!strstr(address,"@")) {
	write_user(user,"~OL~FRWARNING~FW: ~FTNo hostname in e-mail address! (user@myhost.com)~RS\n");
	e++;
	}
if (!strstr(address,".")) {
	write_user(user,"~OL~FRWARNING~FW: ~FTHostname incomplete in e-mail address!~RS\n");
	e++;
	}
if (address[0]=='@') {
	write_user(user,"~OL~FRWARNING~FW: ~FMNo username in e-mail address!~RS\n");
	e++;
	}
if (strstr(address,"root@")) {
	write_user(user,"~OL~FRWARNING~FW: ~FTRoot e-mail accounts not permitted!~RS\n");
	e++;
	}
if (strstr(address,"localhost")) {
	write_user(user,"~OL~FRWARNING~FW: ~FY\"~FGlocalhost~FY\" ~FTdomains not permitted!~RS\n");
	e++;
	}
if (contains_swearing(address)) {
	write_user(user,"~OL~FRWARNING~FW ~FTE-mail address contains swearing!~RS\n");
	return -1;
	}
if (!e) {
	write_user(user,"~OL~FGE-mail address passed the e-mail check!~RS\n");
	return 1;
	}
return 0;
}

hidemail(user)
UR_OBJECT user;
{

if (!strcasecmp(word[2],"off")) {
	if (user->hidemail==0) {
        	sprintf(text,"You are already revealing your e-mail address to everyone.\n");
        	write_user(user,text);
        	return;
        }
	sprintf(text,"You are now revealing your e-mail address to everyone.\n");
	write_user(user,text);
	user->hidemail=0;
	sprintf(text,"[ %s(%s) is now revealing %s e-mail address to everyone. ]\n",user->name,levels[user->level],hisher[user->gender]);
	write_level(ARCH,1,text,NULL);
	return;
	}
if (!strcasecmp(word[2],"on")) {
	if (user->hidemail==1) {
        	sprintf(text,"You are already hiding your e-mail address to all but the Wizards.\n");
        	write_user(user,text);
        	return;
        }
	sprintf(text,"You are now ~OLhiding~RS your e-mail address to all but the Wizards.\n");
	write_user(user,text);
	user->hidemail=1;
	sprintf(text,"[ %s(%s) is now hiding %s e-mail address to all but the Wizards. ]\n",user->name,levels[user->level],hisher[user->gender]);
	write_level(ARCH,1,text,NULL);
	return;
	}
else {
	sprintf(text,"Usage: .set hidemail [ on / off ]\n");
	write_user(user,text);
	return;
	}
}

autofwd(user)
UR_OBJECT user;
{

if (!strcasecmp(word[2],"off")) {
	if (user->autofwd==0) {
        	sprintf(text,"You already have smail auto-forwarding off.\n");
        	write_user(user,text);
        	return;
        }
	sprintf(text,"You have now de-activated the smail auto-forwarding feature.\n");
	write_user(user,text);
	user->autofwd=0;
	sprintf(text,"[ %s(%s) has now de-activated smail auto-forwarding. ]\n",user->name,levels[user->level]);
	write_level(ARCH,1,text,NULL);
	return;
	}
if (!strcasecmp(word[2],"on")) {
	if (user->autofwd==1) {
        	sprintf(text,"You already have smail auto-forwarding on.\n");
        	write_user(user,text);
        	return;
        }
	sprintf(text,"You have now activated the smail auto-forwarding feature.\n");
	write_user(user,text);
	user->autofwd=1;
	sprintf(text,"[ %s(%s) has now activated smail auto-forwarding. ]\n",user->name,levels[user->level]);
	write_level(ARCH,1,text,NULL);
	return;
	}
else {
	sprintf(text,"Usage: .set autofwd [ on / off ]\n");
	write_user(user,text);
	return;
	}
}

/*** Disconnect SUPER USER from talker, secretly ***/
squit(user)
UR_OBJECT user;
{
RM_OBJECT rm;
NL_OBJECT nl;

rm=user->room;
if (user->login) {
	close(user->socket);  
	destruct_user(user);
	num_of_logins--;  
	return;
	}
if (user->type!=REMOTE_TYPE) {
	save_user_details(user,1);  
	write_user(user,"\n~OL~FBSigning off...\n\n");
	close(user->socket);
	if (user->vis) sprintf(text,"~OLSIGN OFF:~RS %s(%s) %s\n",user->name,levels[user->level],user->desc);
	else sprintf(text,"[ ~OLSIGN OFF:~RS (invis) %s(%s) %s\n",user->name,levels[user->level],user->desc);
	write_level(ARCH,1,text,NULL);
	sprintf(text,"%s(%s) secretly logged out.\n",user->name,levels[user->level]);
	write_syslog(text,1);
	if (user->room==NULL) {
		sprintf(text,"REL %s\n",user->name);
		write_sock(user->netlink->socket,text);
		for(nl=nl_first;nl!=NULL;nl=nl->next) 
			if (nl->mesg_user==user) {  
				nl->mesg_user=(UR_OBJECT)-1;  break;  
				}
		}
	}
else {
	write_user(user,"\n~FR~OLYou are pulled back in disgrace to your own domain...\n");
	sprintf(text,"REMVD %s\n",user->name);
	write_sock(user->netlink->socket,text);
	sprintf(text,"~FR~OL%s is banished from here!\n",user->name);
	write_room_except(rm,text,user);
	sprintf(text,"NETLINK: Remote user %s removed.\n",user->name);
	write_syslog(text,1);
	}
if (user->malloc_start!=NULL) free(user->malloc_start);
if (user->vis) num_of_users--;

/* Destroy any clones */
destroy_user_clones(user);
destruct_user(user);
reset_access(rm);
destructed=0;
}

force(user, inpstr)
UR_OBJECT user;
char *inpstr;
{
UR_OBJECT u;

if (word_count<3) {
        write_usage(user,".force <user> <command>\n");
        return;
        }
if (!(u=get_user(word[1]))) {
        write_user(user,notloggedon);
        return;
        }
if (u==user) {
        write_user(user, "Forcing yourself to do something is the forteenth sign of madness.\n");
        return;
        }
inpstr=remove_first(inpstr);
if (u->level>=user->level) {
        write_user(user,"You can't force someone of an equal or higher level to do something.\n");
        return;
        }
clear_words();
word_count=wordfind(inpstr);
sprintf(text, "%s(%s) has forced you to execute: %s\n",user->name,levels[user->level],inpstr);
write_user(u, text);
sprintf(text, "You force %s to execute: %s\n",u->name,inpstr);
write_user(user, text);
sprintf(text,"%s(%s) FORCED %s to execute: %s\n",user->name,levels[user->level],u->name,inpstr);
write_syslog(text,1);
sprintf(text,"[ %s(%s) FORCED %s to execute: %s ]\n",user->name,levels[user->level],u->name,inpstr);
write_level(ARCH,1,text,NULL);
exec_com(u, inpstr);
}

/*** Make someone invisible ***/
makeinvis(user)
UR_OBJECT user;
{
UR_OBJECT u;
 
if (word_count<2) {
        write_usage(user,"makeinvis <user> \n");
        return;
	}
if (user->muzzled) {
	sprintf(text,"You are muzzled, you cannot make someone invisible.\n");
	write_user(user,text);
	return;
	}
if (!(u=get_user(word[1]))) {
	write_user(user,notloggedon);
        return;
	}
if (u==user) {
	write_user(user,"Trying to make yourself invisible is the fifteenth sign of madness.\n");
        return;
        }
if (user->level<=u->level) {
	write_user(user,"You cannot make someone of equal or greater level than youself invisible.\n");
	return;
	}
if (u->level<JRWIZ) {
	write_user(user,"You can't do that to a user.\n"); 
	return;
	} 
if (!u->vis) {
     write_user(user,"That user is already invisible.\n");
     return;
     }
sprintf(text,"~OL~FTYou say some fancy schmancy words and make %s disappear.\n",u->name);
write_user(user,text);
visibility(u,0);
sprintf(text,"%s(%s) made %s invisible.\n",user->name,levels[user->level],u->name);
write_syslog(text,1);
sprintf(text,"[ %s(%s) made %s invisible. ]\n",user->name,levels[user->level],u->name);
write_level(ARCH,1,text,NULL);
}

/*** Make a user visible again ***/
makevis(user)
UR_OBJECT user;
{
UR_OBJECT u;
char *name, *rname;
 
if (user->vis) name=user->name; else name=invisname;
if (word_count<2) {
     write_usage(user,"makevis <user> \n");
     return;
     }
 if (user->muzzled) {
	sprintf(text,"You are muzzled, you cannot make someone visible.\n");
	write_user(user,text);
	return;
	}
if (!(u=get_user(word[1]))) {
	write_user(user,notloggedon);
        return;
	}
if (u->vis) rname=u->name; else rname=invisname;
if (u==user) {
	write_user(user,"Trying to make yourself visible is the sixteenth sign of madness.\n");
        return;
        }
if (u->vis) {
     write_user(user,"That user is already visible.\n");
     return;
     }
if (u->level<JRWIZ) {
	write_user(user,"You can't do that to a user.\n"); 
	return;
	} 
if (user->level<=u->level) {
	write_user(user,"~OL~FRYou cannot make someone of equal or greater level than youself visible\n");
	return;
	}
sprintf(text,"~OL~FTYou mumble some fancy schmancy words and make %s re-appear.\n",rname);
write_user(user,text);
visibility(u,1);
sprintf(text,"%s(%s) made %s visible.\n",user->name,levels[user->level],u->name);
write_syslog(text,1);
sprintf(text,"[ %s(%s) made %s visible. ]\n",user->name,levels[user->level],u->name);
write_level(ARCH,1,text,NULL);
}

/* signal trapping not working, so fork twice */
/* Borrowed from Amnuts 1.42 By Andy Collington */

int double_fork() {
pid_t pid;
int status;

if (!(pid=fork())) {
  switch(fork()) {
      case  0:return 0;
      case -1:_exit(-1);
      default:_exit(0);
      }
  }
if (pid<0||waitpid(pid,&status,0)<0) return -1;
if (WIFEXITED(status))
  if(WEXITSTATUS(status)==0) return 1;
  else errno=WEXITSTATUS(status);
else errno=EINTR;
return -1;
}

/*** "Borrowed" from MoeNUTS ***/
finger_host(user)
UR_OBJECT user;
{
char filename[81];
int i;
if (word_count<2) {
	write_usage(user,"finger user@hostname\n");
	return;
	}
sprintf(text,"~OL~FGGathering finger information for~FT: %s~RS\n",word[1]);
write_user(user,text);
switch(double_fork()) {
  case -1 : write_user(user,"finger: fork failure\n");
	    write_syslog("finger: fork failure\n",1);
	    return;
  case  0 : sprintf(text,"%s fingered \"%s\"\n",user->name,word[1]);
	    write_syslog(text,1);
	    sprintf(text,"[ %s(%s) fingered %s. ]\n",user->name,levels[user->level],word[1]);
	    write_level(ARCH,1,text,NULL);
	    sprintf(filename,"%s/output.%s",TEMPFILES,user->name);	    
	    unlink(filename);
	    sprintf(text,"finger %s > %s",word[1],filename);
            system(text);
	    switch(more(user,user->socket,filename)) {
		case 0: write_user(user,"~OL~FRfinger_host(): could not open temporary finger file...\n");
		case 1: user->misc_op=2;
		}
  	    sprintf(filename,"%s/output.%s",TEMPFILES,user->name);
	    unlink(filename);
            _exit(1);
	    break;
	    }
}

/*** This was "borrowed" from MoeNUTS too. ***/
nslookup(user)
UR_OBJECT user;
{
char filename[81];
int i;
if (word_count<2) {
	write_user(user,"\nName Server Lookup.  This utility is to lookup an IP number\n");
	write_user(user,"address given a host name, or lookup a host name given an IP!\n\n"); 
	if (user->level<WIZARD) write_usage(user,"nslookup <hostname>\n");
	else write_usage(user,"nslookup <hostname> [<nameserver>]\n");
	return;
	}
sprintf(text,"~OL~FGAttempting nameserver lookup for~FT: ~FB\"~FT%s~FB\"\n",word[1]);
write_user(user,text);
switch(double_fork()) {
  case -1 : write_user(user,"nslookup: fork failure\n");
	    write_syslog("nslookup: fork failure\n",1);
	    return;
  case  0 : sprintf(text,"%s performed an nslookup on \"%s\"\n",user->name,word[1]);
	    write_syslog(text,1);
	    sprintf(text,"[ %s(%s) performed an nslookup on %s. ]\n",user->name,levels[user->level],word[1]);
	    write_level(ARCH,1,text,NULL);
	    sprintf(filename,"%s/output.%s",TEMPFILES,user->name);	    
	    unlink(filename);
	    if (word_count>2 && user->level>2) {
      	        sprintf(text,"~OL~FMUsing nameserver~FG: ~FT%s\n",word[2]);
		write_user(user,text);
		sprintf(text,"nslookup %s %s > %s",word[1],word[2],filename);
		}
	    else sprintf(text,"nslookup %s > %s",word[1],filename);
            system(text);
	    switch(more(user,user->socket,filename)) {
		case 0: write_user(user,"~OL~FRnslookup(): could not open temporary file...\n");
		case 1: user->misc_op=2;
		}
  	    sprintf(filename,"%s/output.%s",TEMPFILES,user->name);
	    unlink(filename);
            _exit(1);
	    break;
	    }
}

/*** Clear All The Screens In The Room ***/
/*** "borrowed" from MoeNUTS           ***/
cls_all(user)
UR_OBJECT user;
{
int i;

for(i=0;i<5;++i) write_room(user->room,"\n\n\n\n\n\n\n\n\n\n");
sprintf(text,"[ %s(%s) has cleared everyone's screen... ]\n",user->name,levels[user->level]);
write_level(ARCH,1,text,NULL);
}

int viewfile(user,filename)
UR_OBJECT user;
char *filename;
{
FILE *fp;
char ch, fname[81],ch2[2];
strcpy(fname,filename);

if (!(fp=fopen(fname,"r"))) return 0;
while(!feof(fp)) {
	ch=getc(fp);
	sprintf(ch2,"%c",ch);
	write(user->socket,ch2,strlen(ch2));
	}
fclose(fp);
/* Reset Terminal At End Of File */
write(user->socket,"\13\033[0m",5);
return 1;
}

/*** "borrowed" from MoeNUTS ***/
calendar(user)
UR_OBJECT user;
{
char filename[81];
int i;
if (word_count<2) {
	write_usage(user,"calendar <year>\n");
	write_user(user,"Where: <year> is the year from 1 to 999 to create a calendar for.\n");
	return;
	}
if (!isnumber(word[1])) {
	write_user(user,"~OL~FRCalendar: Year must be between ~FM1 ~FRand ~FM9999~FR!\n");
	return;
	}
if (atoi(word[1])<1 || atoi(word[1])>9999) {
	write_user(user,"~OL~FRCalendar: Year must be between ~FM1 ~FRand ~FM9999~FR!\n");
	return;
	}
sprintf(text,"~OL~FGCreating calendar for the year ~FT%s\n",word[1]);
write_user(user,text);
switch(double_fork()) {
  case -1 : write_user(user,"calendar: fork failure\n");
	    write_syslog("calendar: fork failure\n",1);
	    return;
  case  0 : sprintf(filename,"%s/output.%s",TEMPFILES,user->name);	    
	    unlink(filename);
	    sprintf(text,"cal %s > %s",word[1],filename);
            system(text);
	    viewfile(user,filename);
	    sprintf(text,"%s requested a calendar for year %s.\n",user->name,word[1]);
	    write_syslog(text,1);
	    sprintf(text,"[ %s(%s) has requested a calendar for the year %s. ]\n",user->name,levels[user->level],word[1]);
	    write_level(ARCH,1,text,NULL);
  	    sprintf(filename,"%s/output.%s",TEMPFILES,user->name);
	    unlink(filename);
            _exit(1);
	    break;
	    }
}

fortune(user)
UR_OBJECT user;
{
char filename[81];
int i;
switch(double_fork()) {
  case -1 : write_user(user,"fortune: fork failure\n");
	    write_syslog("fortune: fork failure\n",1);
	    return;
  case  0 : sprintf(text,"~OLYour fortune:~RS\n");
	    write_user(user,text);
	    sprintf(filename,"%s/output.%s",TEMPFILES,user->name);	    
	    unlink(filename);
	    sprintf(text,"/usr/games/fortune > %s",filename);
            system(text);
	    switch(more(user,user->socket,filename)) {
		case 0: write_user(user,"~OL~FRfortune(): could not open temporary fortune file...\n");
		case 1: user->misc_op=2;
		}
  	    sprintf(filename,"%s/output.%s",TEMPFILES,user->name);
	    unlink(filename);
            _exit(1);
	    break;
	    }
}

/*** Make a clone emote ***/
clone_emote(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
RM_OBJECT rm;
UR_OBJECT u;

if (user->muzzled) {
	write_user(user,"You are muzzled, your clone cannot emote.\n");
	return;
	}
if (word_count<3) {
     write_usage(user,"cemote <room clone is in> <message>\n");
	return;
	}
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);
     return;
	}
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE && u->room==rm && u->owner==user) {
		inpstr=remove_first(inpstr);
		if (user->vis) sprintf(text,"Clone of %s %s\n",user->name,inpstr);
		else sprintf(text,"Clone of Someone %s\n",inpstr);
		write_room(u->room,text);
		record(u->room,text);
          	return;
          	}
	}
write_user(user,"You do not have a clone in that room.\n");
}

/* Page a user via the ICQ Network using the pager.mirabilis.com server    */
/* This can be adapted to other talkers, but it requires Andy Collington's */
/* double_fork() function using in Amnuts, Moenuts and RAMTITS based codes */
/* (C)1999 Michael Irving, All Rights Reserved, (C) Moesoft Developments.  */
 
icqpage(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
UR_OBJECT u;
char filename[81],icq_num[50],reciever[USER_NAME_LEN+2]; 
FILE *fp;

if (word_count<2) {
     write_usage(user,".icqpage <user> [<message>]\n");
     return;
     }
if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot send ICQ pages.\n");  
	return;
	}
if ((u=get_user(word[1]))!=NULL) {    /* Online user */
	if (!isnumber(u->icq)) {
	     write_user(user,"\n~OL~FRSorry, user does not have a valid ICQ # set.~RS\n");
	     write_user(user,"~OL~FRThey must have first used .set icq <their icq #>~RS\n");
	     return;
	     }
	sprintf(filename,"%s/icqpage.%s",TEMPFILES,user->name);        
	strcpy(icq_num,u->icq);
	strcpy(reciever,u->name);
	if (!(fp=fopen(filename,"w"))) {
	     write_user(user,"~OL~FRERROR!  Cannot create page message file...\n");
	     return;
	     }
	fprintf(fp,"ICQ Pager for Bolts %s!\n",boltsversion);
	fprintf(fp,"You have been paged by %s from %s.\n",user->name,talkername);
	fprintf(fp,"Telnet to %s for live chat!\n",talkeraddr);
	if (word_count>2) {  /* Only Include Message Header If a message exists */
	     fprintf(fp,"Message:\n");
	     inpstr=remove_first(inpstr);
	     fprintf(fp,"%s\n",inpstr);
	     fprintf(fp,"\nPLEASE DO NOT reply to this e-mail address. Please direct all replies to %s .\n",user->email);
	     }
	fclose(fp);
	send_icq_page(user,icq_num,filename);
  	sprintf(filename,"%s/icqpage.%s",TEMPFILES,user->name);
	unlink(filename);
	return;
	}
if ((u=create_user())==NULL) {      /* user is offline */
	sprintf(text,"%s: unable to create temporary user object.\n",syserror);
        write_user(user,text);
        write_syslog("ERROR: Unable to create temporary user object in icqpage.\n",0);
        return;
        }
word[1][0]=toupper(word[1][0]);
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
        write_user(user,nosuchuser);  
        destruct_user(u);
        destructed=0;
        return;
        }
strcpy(icq_num,u->icq);
strcpy(reciever,u->name);
destruct_user(u);
destructed=0;
if (!isnumber(icq_num)) {
	write_user(user,"\n~OL~FRSorry, user does not have a valid ICQ # set.\n");
	write_user(user,"~OL~FRThey must have first used .set icq <their icq #>\n");
	return;
	}
sprintf(filename,"%s/icqpage.%s",TEMPFILES,user->name);        
if (!(fp=fopen(filename,"w"))) {
	write_user(user,"~OL~FRERROR!  Cannot create page message file...\n");
	return;
	}
fprintf(fp,"ICQ Pager for Bolts %s!\n",boltsversion);
fprintf(fp,"You have been paged by %s from %s.\n",user->name,talkername);
fprintf(fp,"Telnet to %s for live chat!\n",talkeraddr);
if (word_count>2) {  /* Only Include Message Header If a message exists */
	fprintf(fp,"Message:\n");
	inpstr=remove_first(inpstr);
	fprintf(fp,"%s\n",inpstr);
	}
fclose(fp);
send_icq_page(user,icq_num,filename);
sprintf(filename,"%s/icqpage.%s",TEMPFILES,user->name);
unlink(filename);
return;
}

/* Now that we've created the pager info, now it's time to send the page! */
send_icq_page(user,icq_num,fname)
UR_OBJECT user;
char *fname,*icq_num;
{ 

switch(double_fork()) {
  case -1 : return; /* double_fork() failed */
  case  0 :
         sprintf(text,"\n~FTPageing ~FY%s ~RS~OL~FTvia the ICQ Network!\n",icq_num);
         write_user(user,text);
         write_user(user,"~FTIf the user is online they will recieve your message in a couple of minutes.\n");
         write_user(user,"~FTOtherwise, they will get your message next time they log onto the ICQ Network!\n");
         sprintf(text,"Sending an ICQ Page from %s to %s\n",user->name,icq_num);
	 write_syslog(text,1);
         sprintf(text,"mail -s \"ICQ Page From %s\" %s@pager.mirabilis.com < %s",user->name,icq_num,fname);
         system(text);
         _exit(1);
	 return;
	 }
}

traceroute(user)
UR_OBJECT user;
{
char filename[81];
int i;
if (word_count<2) {
	write_usage(user,"traceroute <hostname>\n");
	return;
	}
switch(double_fork()) {
  case -1 : write_user(user,"traceroute: fork failure\n");
	    write_syslog("traceroute: fork failure\n",1);
	    return;
  case  0 : sprintf(text,"%s tracerouted %s\n",user->name,word[1]);
	    write_syslog(text,1);
	    sprintf(text,"[ %s(%s) tracerouted %s. ]\n",user->name,levels[user->level],word[1]);
	    write_level(ARCH,1,text,NULL);
	    sprintf(filename,"%s/output.%s",TEMPFILES,user->name);	    
	    unlink(filename);
	    sprintf(text,"traceroute %s > %s",word[1],filename);
            system(text);
	    switch(more(user,user->socket,filename)) {
		case 0: write_user(user,"~OL~FRtraceroute(): could not open temporary traceroute file...\n");
		case 1: user->misc_op=2;
		}
  	    sprintf(filename,"%s/output.%s",TEMPFILES,user->name);
	    unlink(filename);
            _exit(1);
	    break;
	    }
}

ping(user)
UR_OBJECT user;
{
char filename[81];
int i;
if (word_count<2) {
	write_usage(user,"ping <hostname>\n");
	return;
	}
sprintf(text,"~OL~FGPreparing to ping ~FT%s~RS...\n",word[1]);
write_user(user,text);
switch(double_fork()) {
  case -1 : write_user(user,"ping: fork failure\n");
	    write_syslog("ping: fork failure\n",1);
	    return;
  case  0 : sprintf(text,"%s pinged %s\n",user->name,word[1]);
	    write_syslog(text,1);
	    sprintf(text,"[ %s(%s) pinged %s. ]\n",user->name,levels[user->level],word[1]);
	    write_level(ARCH,1,text,NULL);
	    sprintf(filename,"%s/output.%s",TEMPFILES,user->name);	    
	    unlink(filename);
	    sprintf(text,"ping -c 5 %s > %s",word[1],filename);
            system(text);
	    switch(more(user,user->socket,filename)) {
		case 0: write_user(user,"~OL~FRping(): could not open temporary ping file...\n");
		case 1: user->misc_op=2;
		}
  	    sprintf(filename,"%s/output.%s",TEMPFILES,user->name);
	    unlink(filename);
            _exit(1);
	    break;
	    }
}

/*****************************************************************************/
/*                     MoeNUTS v1.50+ Tic Tac Toe v1.11                      */
/*****************************************************************************/

tictac(user,inpstr)
UR_OBJECT user;
char *inpstr;
{

UR_OBJECT u;
char temp_s[ARR_SIZE];
char *remove_first();
int move;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot play Tic Tac Toe.\n");
	return;
	}
if (word_count<2) {
	write_usage(user,"tictac [<user>] [<#>] [reset][show][status][say]]\n");
	write_user(user,"Examples:  tictac <user>   =  Challenge A User To Tic Tac Toe\n");
	write_user(user,"           tictac <#>      =  Place an 'X' in spot '#'\n");
	write_user(user,"           tictac reset    =  Reset Tic Tac Toe\n");
	write_user(user,"           tictac show     =  Redisplay Board\n");
	write_user(user,"           tictac status   =  Your Tic Tac Toe Status\n");
	write_user(user,"           tictac say      =  Speak To Your Opponent.\n");
	return;
	}
if (strstr(word[1],"reset")) {
	write_user(user,"~OL~FGResetting The Current Tic Tac Toe Game...\n");
	reset_tictac(user);
	if (user->opponent!=NULL) reset_tictac(user->opponent);
	return;
	}
if (strstr(word[1],"show")) {
	if (!user->opponent) {
		write_user(user,"~FRYou are not currently playing Tic Tac Toe!\n");
		return;
		}
	print_tic(user);
	return;
	}
if (strstr(word[1],"stat")) {
	write_user(user,"~OL~FM-----------------~FT[ ~FYYour Tic Tac Toe Statistics ~FT]~FM---------------\n\n");
	sprintf(text,"~FGYou won ~FY%d ~FGgames, lost ~FY%d ~FGgames and had ~FY%d~FG tie games.\n\n",user->twin,user->tlose,user->tdraw);
	write_user(user,text);
	return;
	}
if (strstr(word[1],"say")) {
	if (!user->opponent) {
		write_user(user,"~OL~FRYou have to be playing a game of Tic Tac Toe!\n");
		return;
		}
	inpstr=remove_first(inpstr);
	if (!inpstr[0]) { write_user(user,"Say what to your opponent?\n"); return; }
	sprintf(text,"~OL~FW-> ~FGYou say to ~FT%s~RS~OL~FW: ~FB\"~RS%s~OL~FB\"\n",user->opponent->name,inpstr);
	write_user(user,text);
	sprintf(text,"~OL~FW-> ~FT%s~RS~OL~FG says to you~FW: ~FB\"~RS%s~OL~FB\"\n",user->name,inpstr);
	write_user(user->opponent,text);
	return;
	}
if (atoi(word[1])>9) {
	write_user(user,"~OL~FRThere are only nine spots on the Tic Tac Toe board!\n");
	return;
	}
if (!isdigit(word[1][0])) {
        u=get_user(word[1]);  /* Replaced get_opp. (get_opp = EVIL!) */
	if (u==user) {
		write_user(user,"~OL~FMPlaying Tic Tac Toe with yourself is the eighteenth sign of madness.\n");
		return;
		}
	if (!u->vis) {                              
		write_user(user,notloggedon);
	        return;                             
	        }
	if (u==NULL) {
       		write_user(user,notloggedon);
                return;
      		}
	if (!user->vis) {
		write_user(user,"You can't play this game when you're invisible.\n");
		return;
		}
        reset_tictac(user);
    	user->opponent=u;
    	if (user->opponent->opponent) {
       		if (user->opponent->opponent!=user) {
                        write_user(user, "Sorry, that person is already playing Tic Tac Toe.\n");
           		return;
          		}
               if (user->opponent->level<USER) {
                    write_user(user,"~OL~FRThat user doesn't have Tic Tac Toe!\n");
           		return;
          		}
               sprintf(temp_s, "~OL~FG%s ~FMagrees to play Tic Tac Toe with you\n",user->name);
               write_user(user->opponent,temp_s);
               sprintf(temp_s, "~OL~FMYou agree to a game of Tic Tac Toe with ~FG%s\n",user->opponent->name);
       		write_user(user,temp_s);
               sprintf(temp_s,"~OL~FR%s ~FTstarts playing Tic Tac Toe with ~FR%s\n",user->name,user->opponent->name);
       		write_room(user->room,temp_s);
       		print_tic(user); print_tic(user->opponent);
       		return;
      		}
    	else {
               sprintf(temp_s, "~OL~FG%s ~RS~OL~FMwants to play a game of Tic Tac Toe with you.\nYou can use '.tictac %s' to accept the game!\n",user->name,user->name);
	       	write_user(user->opponent,temp_s);
               sprintf(temp_s,"~OL~FMYou ask ~FG%s ~FMto play a game of Tic Tac Toe.\n",user->opponent->name);
       		write_user(user,temp_s);
       		return;
      		}
   	}   
if (!user->opponent) {
	write_user(user,"~OL~FRYou are not playing Tic Tac Toe with anyone!\n");
    	return;
	}
if (user->opponent->opponent!=user) {
    	write_user(user,"~OL~FRThat user has not accepted yet.\n");
    	return;
   	}
if (!strcmp(user->array,"000000000") && !user->opponent->first) {
    	user->first=1;
   	}
move=word[1][0]-'0';
if (legal_tic(user->array,move,user->first)) {
    	user->array[move-1] = 1;
    	user->opponent->array[move-1] = 2;
     print_tic(user);
    	print_tic(user->opponent);
   	}
else {
    	write_user(user,"~OL~FRThat is an illegal move!\n");
	write_user(user,"~OL~FMIf this is your first move, try .tictac reset and re-start the game!\n");
    	return;
   	}   
if (!win_tic(user->array)) return;
if (win_tic(user->array)==1) {
        sprintf(temp_s, "~OL~FR%s ~RS~OL~FYhas beaten ~FR%s~RS~OL~FY at Tic Tac Toe.\n",user->name,user->opponent->name);
        user->twin++;
        user->opponent->tlose++;
        }
else if (win_tic(user->array) == 2) {
        sprintf(temp_s, "~OL~FR%s ~RS~OL~FYhas beaten~FR %s ~RS~OL~FYat Tic Tac Toe.\n",user->opponent->name, user->name);
        user->opponent->twin++;
        user->tlose++;
        }
else {
        sprintf(temp_s,"~OL~FTIt's a draw between~FR %s ~RS~OL~FT~FTand ~FR%s~RS~OL~FT.\n",user->name,user->opponent->name);
        user->opponent->tdraw++; 
        user->tdraw++;
   	}
write_room(user->room, temp_s);
strcpy(user->array, "000000000");
strcpy(user->opponent->array,"000000000");
user->first=0;
user->opponent->first=0;
user->opponent->opponent=NULL;
user->opponent=NULL;
}

    
legal_tic(char *array,int move,int first) {          
int count1=0,count2=0;
int i;
       
if (array[move-1]==1||array[move-1]==2) return 0;
for (i=0;i<9;i++) {
	if (array[i]==1) count1++;
	else if (array[i]==2) count2++;
	}
if (count1>count2) return 0;
if (first!=1) if (count1==count2) return 0; 
return 1;
}

win_tic(char *array) {
int i,j;
int person;
    
for (person=1;person<3;person++) {        
	for (i=0;i<3;i++) 
		for (j=0;j<3;j++) {
      			if (array[i*3+j]!=person) break;
          		if (j==2) return person;
         		}
	for (i=0;i<3;i++) 
		for (j=0;j<3;j++) {
			if (array[j*3+i]!=person) break;
			if (j==2) return person;
			}  
	if (array[0]==person&&array[4]==person&&array[8]==person)
		return person;
	if (array[2]==person&&array[4]==person&&array[6]==person)
		return person;
	}
for (i=0,j=0;i<9;i++) {
	if (array[i]==1||array[i]==2) j++;
	}
if (j==9) return 3;
return 0;
}

print_tic(user)
UR_OBJECT user;
{          
char temp_s[ARR_SIZE];
char array[10];
int i;
          
for(i=0;i<9;i++) {
     if (user->array[i]==1) array[i]='X';
     else if (user->array[i]==2) array[i]='O';
     else array[i]=' ';
     }
     write_user(user,"\n");
     write_user(user,"~BM~OL~FB.=============================.~RS\n");
     write_user(user,"~BM~OL~FB!   ~FM<~FG1~FM>~FB   |   ~FM<~FG2~FM>~FB   |   ~FM<~FG3~FM>~FB   !~RS\n");
     write_user(user,"~BM~OL~FB!         |         |         !~RS\n");
     sprintf(temp_s, "~BM~OL~FB!    ~FY%c~FB    |    ~FY%c~FB    |    ~OL~FY%c~FB    !~RS    ~OL~FTYour Opponent Is~FW: ~FY%s ~FM(~FYO~FM)\n",array[0],array[1],array[2],user->opponent->name);
     write_user(user,temp_s);
     sprintf(temp_s, "~BM~OL~FB!         |         |         !~RS    ~OL~FTYour  Wins~FW: ~FG%d~FT, Loses~FW: ~FG%d~FT, Draws~FW: ~FG%d\n",user->twin,user->tlose,user->tdraw);
     write_user(user,temp_s);
     sprintf(temp_s, "~BM~OL~FB!---------+---------+---------!~RS    ~OL~FTTheir Wins~FW: ~FG%d~FT, Loses~FW: ~FG%d~FT, Draws~FW: ~FG%d\n",user->opponent->twin,user->opponent->tlose,user->opponent->tdraw);
     write_user(user,temp_s);
     write_user(user,"~BM~OL~FB!   ~FM<~FG4~FM>~FB   |   ~FM<~FG5~FM>~FB   |   ~FM<~FG6~FM>~FB   !~RS\n");
     write_user(user,"~BM~OL~FB!         |         |         !~RS\n");
     sprintf(temp_s, "~BM~OL~FB!    ~FY%c~FB    |    ~FY%c~FB    |    ~FY%c~FB    !~RS\n",array[3],array[4],array[5]);
     write_user(user,temp_s);
     write_user(user,"~BM~OL~FB!         |         |         !~RS\n");
     write_user(user,"~BM~OL~FB!---------+---------+---------!~RS\n");
     write_user(user,"~BM~OL~FB!   ~FM<~FG7~FM>~FB   |   ~FM<~FG8~FM>~FB   |   ~FM<~FG9~FM>~FB   |~RS\n");
     write_user(user,"~BM~OL~FB!         |         |         |~RS\n");
     sprintf(temp_s, "~BM~OL~FB!    ~FY%c~FB    |    ~FY%c~FB    |    ~FY%c~FB    |~RS\n",array[6],array[7],array[8]);
     write_user(user,temp_s);
     write_user(user,"~BM~OL~FB!         |         |         |~RS\n");
     write_user(user,"~BM~OL~FB+=============================+~RS\n");
     write_user(user,"\n");
}

reset_tictac(user)
UR_OBJECT user;
{          
user->opponent=0;
strcpy(user->array,"000000000");
user->first=0;
}

/****************************************************************************/
/*  Hangman For MoeNUTS v1.50+  Code based on code found in Amnuts v1.4.2!  */
/*  Original Code By Andy Collington, MoeNUTS Updates By Michael R. Irving  */
/****************************************************************************/

/* Hangman Graphics (* ASCII Version *) */
char *hanged[8]={
  "\n~FR,---.  \n~FR|   ~FR   ~OL~FM     Hangman\n~FR|~OL~FR           ~FGWord To Guess~FW:~FY %s\n~FR|~OL~FR           ~FBLetters used ~FW:~FT %s \n~FR|~OL       \n~FR+------\n",
  "\n~FR,---.  \n~FR|   ~FR!  ~OL~FM     Hangman\n~FR|~OL~FR           ~FGWord To Guess~FW:~FY %s\n~FR|~OL~FR           ~FBLetters used ~FW:~FT %s \n~FR|~OL       \n~FR+------\n",
  "\n~FR,---.  \n~FR|   ~FR!  ~OL~FM     Hangman\n~FR|~OL~FR   O       ~FGWord To Guess~FW:~FY %s\n~FR|~OL~FR           ~FBLetters used ~FW:~FT %s \n~FR|~OL       \n~FR+------\n",
  "\n~FR,---.  \n~FR|   ~FR!  ~OL~FM     Hangman\n~FR|~OL~FR   O       ~FGWord To Guess~FW:~FY %s\n~FR|~OL~FR   #       ~FBLetters used ~FW:~FT %s \n~FR|~OL       \n~FR+------\n",
  "\n~FR,---.  \n~FR|   ~FR!  ~OL~FM     Hangman\n~FR|~OL~FR   O       ~FGWord To Guess~FW:~FY %s\n~FR|~OL~FR  /#       ~FBLetters used ~FW:~FT %s \n~FR|~OL       \n~FR+------\n",
  "\n~FR,---.  \n~FR|   ~FR!  ~OL~FM     Hangman\n~FR|~OL~FR   O       ~FGWord To Guess~FW:~FY %s\n~FR|~OL~FR  /#\\      ~FBLetters used ~FW:~FT %s\n~FR|~OL       \n~FR+------\n",
  "\n~FR,---.  \n~FR|   ~FR!  ~OL~FM     Hangman\n~FR|~OL~FR   O       ~FGWord To Guess~FW:~FY %s\n~FR|~OL~FR  /#\\      ~FBLetters used ~FW:~FT %s\n~FR|~OL  /    \n~FR+------\n",
  "\n~FR,---.  \n~FR|   ~FR!  ~OL~FM     Hangman\n~FR|~OL~FR   O       ~FGWord To Guess~FW:~FY %s\n~FR|~OL~FR  /#\\      ~FBLetters used ~FW:~FT %s\n~FR|~OL  / \\ \n~FR+------\n"
};

play_hangman(user)
UR_OBJECT user;
{
int i;
char *get_hang_word();

if (word_count<2) {
	write_user(user,"~OL~FMHangman!\nUsage: hangman [play/restart/end/status]\n");
  	return;
	}
srand(time(0));
strtolower(word[1]);
i=0;
if (!strncmp("stat",word[1],4)) {
  if (user->hang_stage==-1) {
    write_user(user,"~OL~FRYou're not currently playing Hangman!\n");
    return;
    }
  write_user(user,"~OL~FGYour current hangman statistics:\n");
  if (strlen(user->hang_guess)<1) sprintf(text,hanged[user->hang_stage],user->hang_word_show,"None yet!");
  else sprintf(text,hanged[user->hang_stage],user->hang_word_show,user->hang_guess);
  write_user(user,text);
  write_user(user,"\n");
  return;
  }
if (!strcmp("end",word[1])) {
  if (user->hang_stage==-1) {
    write_user(user,"~OL~FRYou have to start a game before you can end it!\n");
    return;
    }
  user->hang_stage=-1;
  user->hang_word[0]='\0';
  user->hang_word_show[0]='\0';
  user->hang_guess[0]='\0';
  write_user(user,"~OL~FMYour current game of hangman has ended.\n");
  return;
  }
if (!strcmp("play",word[1])) {
  if (!user->hang_stage==-1) {
    write_user(user,"~OL~FRYou already have a game in progress!\n~OL~FRYou have to end this one before you can start over!\n");
    return;
    }
  get_hang_word(user->hang_word);
  strcpy(user->hang_word_show,user->hang_word);
  for (i=0;i<strlen(user->hang_word_show);++i) user->hang_word_show[i]='-';
  user->hang_stage=0;
  write_user(user,"~OL~FGYour current hangman statistics:\n\n");
  sprintf(text,hanged[user->hang_stage],user->hang_word_show,"None yet!");
  write_user(user,text);
  return;
  }
if (!strncmp("rest",word[1],4)) {
  if (user->hang_stage!=-1) {
	write_user(user,"~OL~FMYour Current Game of Hangman Has Eneded...\n");
	write_user(user,"~OL~FTStarting A New Game Of Hangman...\n");
	}
  else write_user(user,"~OL~FGYou weren't currently playing...Starting a new game of hangman...\n");
  /* Clear The Current Game Stats */
  user->hang_stage=-1;
  user->hang_word[0]='\0';
  user->hang_word_show[0]='\0';
  user->hang_guess[0]='\0';
  /* Start A New Game */
  get_hang_word(user->hang_word);
  strcpy(user->hang_word_show,user->hang_word);
  for (i=0;i<strlen(user->hang_word_show);++i) user->hang_word_show[i]='-';
  user->hang_stage=0;
  write_user(user,"Your current hangman game stats:\n\n");
  sprintf(text,hanged[user->hang_stage],user->hang_word_show,"None yet!");
  write_user(user,text);
  return;
  }
write_user(user,"~OL~FMHangman!\nUsage: hangman [play/restart/end/status]\n");
}

/* returns a word from a list for hangman.  this will save loading words
   into memory, and the list could be updated as and when you feel like it */

char *get_hang_word(aword)
char *aword; 
{
char filename[80];
FILE *fp;
int lines,cnt,i;

lines=cnt=i=0;
sprintf(filename,"%s/%s",DATAFILES,HANGDICT);
lines=count_lines(filename);
srand(time(0));
cnt=rand()%lines;

if (!(fp=fopen(filename,"r"))) return("moenuts");
fscanf(fp,"%s\n",aword);
while (!feof(fp)) {
  if (i==cnt) {
    fclose(fp);
    return aword;
    }
  ++i;
  fscanf(fp,"%s\n",aword);
  }
fclose(fp);
/* if no word was found, just return a generic word */
return("superman");
}

/* counts how many lines are in a file */
int count_lines(filename)
char *filename;
{
int i,c;
FILE *fp;

i=0;
if (!(fp=fopen(filename,"r"))) return i;
c=getc(fp);
while (!feof(fp)) {
  if (c=='\n') i++;
  c=getc(fp);
  }
fclose(fp);
return i;
}

/* Lets a user guess a letter for hangman */
guess_hangman(user)
UR_OBJECT user;
{
int count,i,blanks;

count=blanks=i=0;
if (word_count<2) {
  write_usage(user,"guess <letter>\n");
  return;
  }
if (user->hang_stage==-1) {
  write_user(user,"~OL~FRYou're not playing hangman at the moment!\n~OL~FRTry: ~FM.hangman start\n");
  return;
  }
if (strlen(word[1])>1) {
  write_user(user,"~OL~FRHey!! Hey!! Hey!! ~FMOne letter at a time please!!\n");
  return;
  }
if (atoi(word[1])>0 || word[1][0]=='0') {
  write_user(user,"~OL~FRHey!  Guess 'LETTERS'! Not 'NUMBERS'! :)\n");
  return;
  }
strtolower(word[1]);
if (strstr(user->hang_guess,word[1])) {
  user->hang_stage++;
  write_user(user,"~OL~FRYou have already guessed that letter!  ~FMAnd you know what that means...\n\n");
  sprintf(text,hanged[user->hang_stage],user->hang_word_show,user->hang_guess);
  write_user(user,text);
  if (user->hang_stage>=7) {
    write_user(user,"~FM-~OL=~FR[ ~FY~LISNAP ~RS~OL~FR]~FM=~RS~FM-  ~OL~FTThe snap of your neck is heard as you are hanged!\n~OL~FMYou did not guess the word and have died...\n\n");
    user->hang_stage=-1;
    user->hang_word[0]='\0';
    user->hang_word_show[0]='\0';
    user->hang_guess[0]='\0';
    }
  write_user(user,"\n");
  return;
  }
for (i=0;i<strlen(user->hang_word);++i) {
  if (user->hang_word[i]==word[1][0]) {
    user->hang_word_show[i]=user->hang_word[i];
    ++count;
    }
  if (user->hang_word_show[i]=='-') ++blanks;
  }
strcat(user->hang_guess,word[1]);
if (!count) {
  user->hang_stage++;
  write_user(user,"~OL~FROoh, Tough luck!  ~FMThat letter isn't in the word!\n~OL~FRAnd you know what that means...\n");
  sprintf(text,hanged[user->hang_stage],user->hang_word_show,user->hang_guess);
  write_user(user,text);
  if (user->hang_stage>=7) {
    write_user(user,"~FM-~OL=~FR[ ~FY~LISNAP ~RS~OL~FR]~FM=~RS~FM-  ~OL~FTThe snap of your neck is heard as you are hanged!\n~OL~FMYou did not guess the word and have died...\n\n");
    user->hang_stage=-1;
    user->hang_word[0]='\0';
    user->hang_word_show[0]='\0';
    user->hang_guess[0]='\0';
    }
  write_user(user,"\n");
  return;
  }
if (count==1) sprintf(text,"~OL~FYWooHoo!  ~FGThere's 1 ~FB\"~FT%s~FB\"~FG in the word!\n",word[1]);
else sprintf(text,"~OL~FYWooHoo!  ~FGThere's ~FM%d ~FB\"~FT%s~FB\"~FG's in the word!\n",count,word[1]);
write_user(user,text);
sprintf(text,hanged[user->hang_stage],user->hang_word_show,user->hang_guess);
write_user(user,text);
if (!blanks) {
  write_user(user,"~FG~OLCongratulations! ~FB-~FM=~FT*~FM=~FB- ~FYYou've escaped death and guessed the word!\n");
  user->hang_stage=-1;
  user->hang_word[0]='\0';
  user->hang_word_show[0]='\0';
  user->hang_guess[0]='\0';
  }
}

reset_hangman(user)
UR_OBJECT user;
{
  user->hang_stage=-1;
  user->hang_word[0]='\0';
  user->hang_word_show[0]='\0';
  user->hang_guess[0]='\0';
}
/***************************************************************************/
/******************************* End Of Hangman ****************************/
/***************************************************************************/

depositbank(user,amount,type)
UR_OBJECT user;
int amount,type;
{
int oldamt,newamt;

oldamt=0; newamt=0;
oldamt=user->bank_balance;
newamt=oldamt+amount;
user->bank_balance=newamt;
if (!type) {
	write_user(user,"\n~OL~FYFirst National Bank Account Transaction Record\n");
	write_user(user,"~BW~FK+---------------------------------------------------------------+~RS\n");
	write_user(user,"~BW~FK|  Transaction            | Transaction Amt  | Account Balance  |~RS\n");
	write_user(user,"~BW~FK|=========================|==================|==================|~RS\n");
	sprintf(text,"~BW~FK|  Cash Deposit           |  $%5d.00       |  $%5d.00       |~RS\n",amount,newamt);  
	write_user(user,text);
	write_user(user,"~BW~FK+---------------------------------------------------------------+~RS\n\n");
	write_user(user,"~OL~FMThank you for using First National...~FGHave A Nice Day!\n\n");
	}
save_user_details(user,1);
}

withdrawlbank(user,amount,type)
UR_OBJECT user;
int amount,type;
{
int oldamt,newamt;

oldamt=0; newamt=0;
oldamt=user->bank_balance;
newamt=oldamt-amount;
user->bank_balance=newamt;
if (!type) {
	write_user(user,"\n~OL~FYFirst National Bank Account Transaction Record\n");
	write_user(user,"~BW~FK+---------------------------------------------------------------+~RS\n");
	write_user(user,"~BW~FK|  Transaction            | Transaction Amt  | Account Balance  |~RS\n");
	write_user(user,"~BW~FK|=========================|==================|==================|~RS\n");
	sprintf(text,"~BW~FK|  Cash Withdrawal        |  $%5d.00       |  $%5d.00       |~RS\n",amount,newamt);  
	write_user(user,text);
	write_user(user,"~BW~FK+---------------------------------------------------------------+~RS\n\n");
	write_user(user,"~OL~FMThank you for using First National...~FGHave A Nice Day!\n\n");
	}
save_user_details(user,1);
}

givecash(user)
UR_OBJECT user;
{
UR_OBJECT u;
char *name;
int amt,bank;
amt=0; bank=0;

if (word_count<3) {
     write_usage(user,".givecash <user> <amount>\n");
     return;
     }
if (!(u=get_user(word[1]))) {
     write_user(user,notloggedon);
     return;
     }
if (u==user && user->level<ARCH) {
	write_user(user,"~OL~FRYou cannot give yourself money!  NICE TRY!\n");
	return;
	}
if ((user->level>=u->level) &&  user->level<ARCH) {
     write_user(user,"You cannot give money to anyone the same or higher level than you!\n");
     sprintf(text,"%s tried to give you some money!  What a nice person eh?\n",user->name);
	write_user(u,text);
	sprintf(text,"%s tried to give %s some money!\n",user->name,u->name);
	write_syslog(text,1);
     return;
     }
amt=atoi(word[2]);
bank=u->bank_balance;
if (bank>100 && user->level<ARCH) { write_user(user,"They've got more than $100 alreday!\n"); return; }
if (amt<10) { write_user(user,"~OL~FRGawd You're Cheap!  You have to give more than $10!\n"); return; }
if (amt>100 && user->level<ARCH) { write_user(user,"~OL~FRUgh! More than $100?  Wanna break us?\n"); return; }
sprintf(text,"~OL~FG%s ~RS~OL~FGhas just deposited $%d.00 into your bank account.\n",user->name,amt);
write_user(u,text);
sprintf(text,"~OL~FGYou deposit $%d.00 into %s~RS~OL~FG's account.\n",amt,u->name);
write_user(user,text);
depositbank(u,amt,0);
sprintf(text,"~OL[ %s has just deposited $%d.00 into %s's bank account ]~RS\n",user->name,amt,u->name);
write_level(ARCH,1,text,NULL);
}

lendcash(user)
UR_OBJECT user;
{
UR_OBJECT u;
char *name;
int amt,bank,deposit;
amt=0; bank=0; deposit=0;

if (word_count<3) {
        write_usage(user,".lend <user> <amount>\n");
        return;
        }
if (!(u=get_user(word[1]))) {
        write_user(user,notloggedon);
        return;
        }
if (u==user) {
	write_user(user,"~OL~FRLoaning yourself money is the ninteenth sign of madness.\n");
	return;
	}
if ((user->level>=u->level) &&  user->level<ARCH) {
        write_user(user,"You cannot give money to anyone the same or higher level than you!\n");
        sprintf(text,"%s tried to give you some money!  What a nice person eh?\n",user->name);
	write_user(u,text);
	sprintf(text,"%s tried to give %s some money!\n",user->name,u->name);
	write_syslog(text,1);
        return;
        }
amt=atoi(word[2]);
bank=user->bank_balance;
if (amt<10) {
	write_user(user,"~OL~FRYou cannot lend any less than $10.00!\n");
	return;
	}
if (amt>100) {
	write_user(user,"~OL~FRMore than $100?  Make'em work for their money!\n");
	return;
	}
if (amt>bank) {
	write_user(user,"You don't have that kind of money in your bank account!\n");
	return;
	}
sprintf(text,"~OL~FG%s ~RS~OL~FGhas just deposited $%d.00 into your bank account.\n",user->name,amt);
write_user(u,text);
sprintf(text,"~OL~FGYou deposit $%d.00 into %s~RS~OL~FG's account.\n",amt,u->name);
write_user(u,text);
withdrawlbank(user,amt,0);
depositbank(u,amt,0);
sprintf(text,"~OL[ %s has just deposited $%d.00 into %s's bank account ]~RS\n",user->name,amt,u->name);
write_level(ARCH,1,text,NULL);
}

balance(user)
UR_OBJECT user;
{
if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot ask for a balance.\n");  
	return;
	}
write_user(user,"\n~OL~FYFirst National Bank Account Transaction Record\n");
write_user(user,"~BW~FK+---------------------------------------------------------------+~RS\n");
write_user(user,"~BW~FK|  Transaction            | Transaction Amt  | Account Balance  |~RS\n");
write_user(user,"~BW~FK|=========================|==================|==================|~RS\n");
sprintf(text,"~BW~FK|  Balance                |                  |  $%5d.00       |~RS\n",user->bank_balance);  
write_user(user,text);
write_user(user,"~BW~FK+---------------------------------------------------------------+~RS\n\n");
write_user(user,"~OL~FMThank you for using First National...~FGHave A Nice Day!\n\n");
}

/*** print out greeting in large letters ***/
greet(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char pbuff[ARR_SIZE],temp[8], *name;
int slen,lc,c,i,j;
char *clr[]={"~OL~FM","~OL~FG","~OL~FM","~OL~FY","~OL~FR"};
char *greet_style          ="~OL~FB%s announces~FR:\n\n";

if (user->muzzled) {
   write_user(user,"You are muzzled, you cannot greet.\n");  return;
   }
if (word_count<2) {
   write_usage(user,"greet <message>\n"); return;
   }
if (ban_swearing && contains_swearing(inpstr)) {
    write_user(user,noswearing);
    return;
    }
if (strlen(inpstr)>11) {
    write_user(user,"You can only have up to 11 letters in the greet.\n");
    return;
    }
if (user->vis) name=user->name; else name=invisname;

if (user->vis) {
     sprintf(text,greet_style,name);
	write_room(user->room,text);
	}
slen=strlen(inpstr);
if (slen>11) slen=11;
for (i=0; i<5; ++i) {
   pbuff[0] = '\0';
   temp[0]='\0';
   for (c=0; c<slen; ++c) {
     /* check to see if it's a character a-z */
     if (isupper(inpstr[c]) || islower(inpstr[c])) {
       lc = tolower(inpstr[c]) - 'a';
       if ((lc >= 0) && (lc < 27)) {
         for (j=0;j<5;++j) {
	   if(biglet[lc][i][j]) {
	     sprintf(temp,"%s#",clr[rand()%5]);
	     strcat(pbuff,temp);
	     }
	   else strcat(pbuff," ");
	   }
         strcat(pbuff,"  ");
         }
       }
     /* check if it's a character from ! to @ */
     if (isprint(inpstr[c])) {
       lc = inpstr[c] - '!';
       if ((lc >= 0) && (lc < 32)) { 
         for (j=0;j<5;++j) {
	   if(bigsym[lc][i][j]) {
	     sprintf(temp,"%s#",clr[rand()%5]);
	     strcat(pbuff,temp);
	     }
	   else strcat(pbuff," ");
	   }
         strcat(pbuff,"  ");
         }
       }
     }
   sprintf(text,"%s\n",pbuff);
   write_room(user->room,text);
   }
write_room(user->room,"\n");
if (user->vis) {
	strtoupper(inpstr);
        sprintf(text,"%s announces: %s",name,inpstr);
	record(user->room,text);
	}
}

/*** Alerting ***/
alert(user)
UR_OBJECT user;
{
  char filename[80];

  if(word_count<2)
    sprintf(filename,"%s/c_%s",DATAFILES,ALERTFILE);
  else
    sprintf(filename,"%s/%c_%s",DATAFILES,word[1][0],ALERTFILE);
  if(!more(user,user->socket,filename))
    {
       write_user(user,"Unknown alert type, yet...\n");
       return;
    }
  user->afk=1;
  user->ignall=1;
  strcpy(user->afk_mesg,"I'm being watched");
  sprintf(text,"%s is watched by the alien creatures!\n",user->vis?user->name:invisname,levels[user->level]);
  write_room_except(user->room,text,user);
  user->misc_op=8;
}

/*** modify user information                         ***/
/*** nicked from GAEN talker                         ***/
/*** GAEN page: http://www.infoiasi.ro/~busaco/gaen/ ***/
modify(user)
UR_OBJECT user;
{
  UR_OBJECT u;
  char s[OUT_BUFF_SIZE+1],filename[80],newfilename[80];
  char extens[]="DPM";
  int i,hours,days,no_exists=0;
  FILE *fp;
  if(word_count<4)
    {
      write_user(user,"~OLUsage:~RS modify <user> desc/in/out/site/vis/time/level/ident <text>\n");
      return;
    }
  if((u=get_user(word[1]))==NULL)
    {
     /* User not logged on */
     if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user object.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user object in modify().\n",0);
	return;
	}
     strcpy(u->name,word[1]);
     if (!load_user_details(u)) {
	write_user(user,nosuchuser);
	destruct_user(u);
	destructed=0;
	return;
	}
     no_exists=1;	
    }
  if(u->level>=user->level)
    {
      write_user(user,"You cannot modify the information about this user!\n");
      if(no_exists)
        {
          destruct_user(u);
          destructed=0;
        }  
      return;
    }
  strcpy(s,word[3]);
  for(i=4;i<word_count;i++)
    {
      strcat(s," ");
      strcat(s,word[i]);
    }
  if(!strncmp(word[2],"desc",strlen(word[2])))
    {
      if(strlen(s)>USER_DESC_LEN) s[USER_DESC_LEN]='\0';
      strcpy(u->desc,s);
      sprintf(text,"You change %s's description...\n",u->name);
      write_user(user,text);
      sprintf(text,"~OL[ %s(%s) has changed %s's description to: %s ]~RS\n",user->name,levels[user->level],u->name,u->desc);
      write_level(ARCH,1,text,NULL);
      if(no_exists) 
      {  
         save_user_details(u,0);
         destruct_user(u);
         destructed=0;
      }   
      return;
    }
  if(!strncmp(word[2],"in",strlen(word[2])))
    {
      if(strlen(s)>PHRASE_LEN) s[PHRASE_LEN]='\0';    
      strcpy(u->in_phrase,s);
      sprintf(text,"You change %s's in phrase...\n",u->name);
      write_user(user,text);
      sprintf(text,"~OL[ %s(%s) has changed %s's in phrase to: %s ]~RS\n",user->name,levels[user->level],u->name,u->in_phrase);
      write_level(ARCH,1,text,NULL);
      if(no_exists) 
      {  
         save_user_details(u,0);
         destruct_user(u);
         destructed=0;
      }
      return;
    }        
  if(!strncmp(word[2],"out",strlen(word[2])))    
    {
      if(strlen(s)>PHRASE_LEN) s[PHRASE_LEN]='\0';    
      strcpy(u->out_phrase,s);
      sprintf(text,"You change %s's out phrase...\n",u->name);
      write_user(user,text);
      sprintf(text,"~OL[ %s(%s) has changed %s's out phrase to: %s ]~RS\n",user->name,levels[user->level],u->name,u->out_phrase);
      write_level(ARCH,1,text,NULL);
      if(no_exists) 
      {  
         save_user_details(u,0);
         destruct_user(u);
         destructed=0;
      }
      return;
    }            
  if(!strncmp(word[2],"site",strlen(word[2])))
    {
      if(strlen(s)>SITE_NAME_LEN) s[SITE_NAME_LEN]='\0';    
      strcpy(u->site,s);
      strcpy(u->site,s);
      sprintf(text,"You change %s's site...\n",u->name);
      write_user(user,text);
      sprintf(text,"~OL[ %s(%s) has changed %s's site to %s ]~RS\n",user->name,levels[user->level],u->name,u->site);
      write_level(ARCH,1,text,NULL);
      if(no_exists) 
      {  
         save_user_details(u,0);
         destruct_user(u);
         destructed=0;
      }
      return;
    }  
  if(!strncmp(word[2],"time",strlen(word[2])))
    {
      if(word_count<5)
        {
           write_user(user,"~OLUsage:~RS modify <user> time <days> <hours>\n");
           if(no_exists)
        	{
          		destruct_user(u);
          		destructed=0;
        	}  
           return;
        }
      hours=atoi(word[4]);
      if(hours<0 || hours>23) 
        hours=0;
      days=atoi(word[3]);
      if(days<0)
        days=0;
      u->total_login=days*86400+hours*3600;       
      sprintf(text,"You change %s's login time...\n",u->name);
      write_user(user,text);
      sprintf(text,"%s changed %s's login time: %d days, %d hours.\n",user->name,u->name,days,hours);
      write_syslog(text,1);
      sprintf(text,"~OL[ %s(%s) has changed %s's login time to %d days, %d hours ]~RS\n",user->name,levels[user->level],u->name,days,hours);
      write_level(ARCH,1,text,NULL);
      if(no_exists) 
      {  
         save_user_details(u,0);
         destruct_user(u);
         destructed=0;
      }
      return;
    }  
  if(!strncmp(word[2],"vis",strlen(word[2])))
    {
      if(no_exists)
        {
          write_user(user,"Use this option only for logged users!\n");
          destruct_user(u);
          destructed=0;
          return;
        }
      if(!strcmp(word[3],"on"))
        {
          if(u->vis)
            {
              write_user(user,"That user is already visible.\n");
              return;
            }
          u->vis=1;
  	  num_of_users++;
          write_user(u,"~FB~OLThe holy spirit of this talker makes you visible!\n");
          sprintf(text,"%s is visible now!\n",u->name);
          write_user(user,text);
          sprintf(text,"~OL[ %s(%s) has made %s visible.]~RS\n",user->name,levels[user->level],u->name);
          write_level(ARCH,1,text,NULL);
          return;
        }
      if(!strcmp(word[3],"off"))
        {
          if(!u->vis)
            {
              write_user(user,"That user is already invisible.\n");
              return;
            }
  	  num_of_users--;
          u->vis=0;
          write_user(u,"~FB~OLThe holy spirit of this talker makes you invisible!\n");
          sprintf(text,"%s is invisible now!\n",u->name);
          write_user(user,text);
          sprintf(text,"~OL[ %s(%s) has made %s invisible.]~RS\n",user->name,levels[user->level],u->name);
          write_level(ARCH,1,text,NULL);
          return;
        }
      write_user(user,"~OLUsage:~RS modify <user> vis on/off\n");
      return;
    } 
  if(!strncmp(word[2],"ident",strlen(word[2])))
    {
      if(!no_exists)
        {
          write_user(user,"Use this option only for not logged users!\n");
          return;
        }
      destruct_user(u);
      destructed=0;  
      if(!strcmp(word[1],word[3]))
        {
          write_user(user,"The old and new names are not different.\n");
          return;
        }    
      if(strlen(word[3])<USER_MIN_LEN)
        {
          write_user(user,"Name length too short.\n"); 
          return;
        }   
      if(strlen(word[3])>USER_NAME_LEN)
        word[3][USER_NAME_LEN]='\0';
      for(i=0;i<strlen(word[3]);i++)
        if(!isalpha(word[3][i]))
          {
            write_user(user,"New user name must contains only letters!\n");
            return;
          }
      word[3][0]=toupper(word[3][0]);
      sprintf(newfilename,"%s/%s.D",USERFILES,word[3]);        
      if((fp=fopen(newfilename,"r"))!=NULL)
        {
           write_user(user,"That name is already used!\n");
           fclose(fp);
           return;
        }   
       sprintf(filename,"%s/%s.D",USERFILES,word[1]);
       for(i=0;i<strlen(extens);i++)
         {
           filename[strlen(filename)-1]=extens[i];
           newfilename[strlen(newfilename)-1]=extens[i];
           rename(filename,newfilename);
         }
       sprintf(text,"%s's identity changed into %s.\n",word[1],word[3]);
       write_user(user,text);
       sprintf(text,"~%s's identity changed into %s by %s.\n",
       		word[1],word[3],user->name);       
       write_syslog(text,1);
       sprintf(text,"~OL[ %s(%s) has changed %s's name to %s ]~RS\n",user->name,levels[user->level],word[1],word[3]);
       write_level(ARCH,1,text,NULL);
       return;       
    }                
  if(!strncmp(word[2],"level",strlen(word[2])))
    {
      if((i=get_level(word[3]))==-1 || i>user->level)
        {
          write_user(user,"Invalid level.\n");
          if(no_exists)
            {
              destruct_user(u);
              destructed=0;
            }
          return;
        }
      u->level=i; /* silently promote/demote an user */
      u->level=i;
      sprintf(text,"You change %s's level...\n",u->name);
      write_user(user,text);
      sprintf(text,"%s's level changed to %s by %s.\n",word[1],level_name[u->level],user->name);
      write_syslog(text,1);
      sprintf(text,"[ %s(%s) has changed %s's level to %s(%s). ]\n",user->name,levels[user->level],u->name,level_name[u->level],levels[u->level]);
      write_level(ARCH,1,text,NULL);
      if (no_exists)
         {
           save_user_details(u,0);
           destruct_user(u);
           destructed=0;
         }  
      return;
    }                          
  write_user(user,"Unknown argument, yet!...\n");
  if(no_exists)
    {
       destruct_user(u);
       destructed=0;
    }  
}

/*** Save users details files ***/
savedetails(user)
UR_OBJECT user;
{
   UR_OBJECT u;
   if(word_count<2)	/* all users */
     {
      sprintf(text,"[ Save all users details initiated by %s. ]\n",
                user->vis?user->name:invisname);
      write_level(ARCH,1,text,NULL);
      sprintf(text,"%s saved all users details.\n",user->name);
      write_syslog(text,1);
      write_user(user,"Save details for...");
      for(u=user_first;u!=NULL;u=u->next)	
        {
           if(u->type!=USER_TYPE || u->login) 
             continue;
           sprintf(text," %s",u->name);
           write_user(user,text);
           save_user_details(u,1);
        }
     } 
    else	/* only one user */
     {
      if((u=get_user(word[1]))==NULL)
	{
	  write_user(user,nosuchuser);
	  return;
	}
       sprintf(text,"[ Save %s's details initiated by %s ]\n",
               u->name,user->vis?user->name:invisname);	
       write_level(ARCH,1,text,NULL);
       sprintf(text,"%s saved %s's details.\n",user->name,hisher[user->gender]); 
       write_syslog(text,1);
       sprintf(text,"Save details for... %s",u->name);
       write_user(user,text);
       save_user_details(u,1);
      } 	  
    write_user(user,".\n");  
}

/*** Write usage of a command ***/
write_usage(user,msg)
UR_OBJECT user;
char *msg;
{
  sprintf(text,"~OLUsage:~RS ~FT%s",msg);
  write_user(user,text);
}

/** put speech in a think bubbles **/
think(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char *name;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot think.\n"); return;
	}
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	} 
if (user->vis) name=user->name; else name=invisname;
if (word_count<2) {
  write_user(user,"Think what?\n");
  return;
  }
else {
  sprintf(text,"%s thinks . o O ( %s )\n",name,inpstr);
  write_room(user->room,text);
  }
}

mumble(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char *name;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot mumble.\n"); return;
	}
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	} 
if (user->vis) name=user->name; else name=invisname;
if (word_count<2) {
  write_user(user,"Mumble what?\n");
  return;
  }
else {
  sprintf(text,"%s mumbles something about %s.\n",name,inpstr);
  write_room(user->room,text);
  }
}

sing(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char *name;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot sing.\n"); return;
	}
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	} 
if (user->vis) name=user->name; else name=invisname;
if (word_count<2) write_user(user,"Sing what?\n");
else {
  sprintf(text,"%s sings ... hey hey yeah %s oh yeah ...\n",user->name,inpstr);
  write_room(user->room,text);
  }
}

/*** Broadcast an important message ***/
bbcast(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char *b="\007";

if (word_count<2) {
    write_usage(user,"bbcast <message>\n");  return;
  }
/* wizzes should be trusted... But they ain't! */
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);
	return;
	}
force_listen=1;

sprintf(text,"%s~FR~OL--==<~RS %s ~RS~FR~OL>==--\n",b,inpstr);
write_room(NULL,text);
}

/*** stop zombie processes ***/
int send_forward_email(char *send_to, char *mail_file)
{
  switch(double_fork()) {
    case -1 : unlink(mail_file); return -1; /* double_fork() failed */
    case  0 : sprintf(text,"mail %s < %s",send_to,mail_file);
              system(text);
	      unlink(mail_file);
	      _exit(1);
	      break; /* should never get here */
    default: break;
    }
return 1;
}

/* allows a user to email specific messages to themselves */
forward_specific_mail(user)
UR_OBJECT user;
{
FILE *fpi,*fpo;
int valid,cnt,total,smail_number,tmp1,tmp2;
char w1[ARR_SIZE],line[ARR_SIZE],filenamei[80],filenameo[80];

if (word_count<2) {
  write_usage(user,"fmail all/<mail number>\n");
  return;
  }
sprintf(filenamei,"%s/%s.M",USERFILES,user->name);
if (!(fpi=fopen(filenamei,"r"))) {
	write_user(user,"You currently have no mail.\n");  return;
	}
/* send all smail */ 
if (!strcasecmp(word[1],"all")) {
  sprintf(filenameo,"%s/%s.FWD",MAILSPOOL,user->name);
  if (!(fpo=fopen(filenameo,"w"))) {
    write_syslog("Unable to open forward mail file in forward_specific_mail()\n",0);
    write_user(user,"Sorry, could not forward any mail to you.\n");
    return;
    }
  sprintf(filenamei,"%s/%s.M",USERFILES,user->name);
  if (!(fpi=fopen(filenamei,"r"))) {
    write_user(user,"Sorry, could not forward any mail to you.\n");
    sprintf(text,"Unable to open %s's mailbox in forward_specific_mail()\n",user->name);
    write_syslog(text,0);
    fclose(fpo);
    return;
    }
  fprintf(fpo,"From: %s\n",talkername);
  fprintf(fpo,"To: %s <%s>\n",user->name,user->email);
  fprintf(fpo,"Subject: Manual forwarding of smail.\n\n\n");
  fscanf(fpi,"%d %d\r",&tmp1,&tmp2);
  fgets(line,ARR_SIZE-1,fpi);
  while (!feof(fpi)) {
    fprintf(fpo,"%s",colour_com_strip(line));
    fgets(line,ARR_SIZE-1,fpi);
    }
  fputs(talker_signature,fpo);
  fclose(fpi);
  fclose(fpo);
  send_forward_email(user->email,filenameo);
  write_user(user,"You have now sent ~OL~FRall~RS your smails to your e-mail account.\n");
  return;
  }
/* send just a specific smail */
smail_number=atoi(word[1]);
if (!smail_number) {
  write_usage(user,"fmail all/<mail number>\n");
  return;
  }
if (smail_number>total) {
  sprintf(text,"You only have %d messages in your mailbox.\n",total);
  write_user(user,text);
  return;
  }
sprintf(filenameo,"%s/%s.FWD",MAILSPOOL,user->name);
if (!(fpo=fopen(filenameo,"w"))) {
  write_syslog("Unable to open forward mail file in forward_specific_mail()\n",0);
  write_user(user,"Sorry, could not forward any mail to you.\n");
  return;
  }
sprintf(filenamei,"%s/%s.M",USERFILES,user->name);
if (!(fpi=fopen(filenamei,"r"))) {
  write_user(user,"Sorry, could not forward any mail to you.\n");
  sprintf(text,"Unable to open %s's mailbox in forward_specific_mail()\n",user->name);
  write_syslog(text,0);
  fclose(fpo);
  return;
  }
fprintf(fpo,"From: %s\n",talkername);
fprintf(fpo,"To: %s <%s>\n",user->name,user->email);
fprintf(fpo,"Subject: Manual forwarding of smail.\n\n\n");
valid=1;  cnt=1;
fscanf(fpi,"%d %d\r",&tmp1,&tmp2);
fgets(line,ARR_SIZE-1,fpi);
while(!feof(fpi)) {
  if (*line=='\n') valid=1;
  sscanf(line,"%s",w1);
  if (valid && (!strcmp(w1,"~OLFrom:") || !strcmp(w1,"From:"))) {
    if (smail_number==cnt) {
      while(*line!='\n') {
	fprintf(fpo,"%s",colour_com_strip(line));
	fgets(line,ARR_SIZE-1,fpi);
        }
      }
    valid=0;  cnt++;
    if (cnt>smail_number) goto SKIP; /* no point carrying on if read already */
    }
  fgets(line,ARR_SIZE-1,fpi);
  }
SKIP:
fputs(talker_signature,fpo);
fclose(fpi);
fclose(fpo);
send_forward_email(user->email,filenameo);
sprintf(text,"You have now sent smail number ~FM~OL%d~RS to your email account.\n",smail_number);
write_user(user,text);
}

/*** send smail to the email ccount ***/
forward_email(name,from,message)
char *name, *from, *message;
{
FILE *fp;
UR_OBJECT u;
char filename[80];
int on=0;

if ((u=get_user(name))) {
  on=1;
  goto SKIP;
  }
/* Have to create temp user if not logged on to check if email verified, etc */
if ((u=create_user())==NULL) {
  write_syslog("ERROR: Unable to create temporary user object in forward_email().\n",0);
  return;
  }
strcpy(u->name,name);
if (!load_user_details(u)) {
  destruct_user(u);
  destructed=0;
  return;
  }
on=0;
SKIP:
if (!u->autofwd){
  if (!on) { destruct_user(u);  destructed=0; }
  return;
  } 

sprintf(filename,"%s/%s.FWD",MAILSPOOL,u->name);
if (!(fp=fopen(filename,"w"))) {
  write_syslog("Unable to open forward mail file in set_forward_email()\n",0);
  return;
  }
fprintf(fp,"From: %s\n",talkername);
fprintf(fp,"To: %s <%s>\n",u->name,u->email);
fprintf(fp,"Subject: Auto-forward of smail.\n");
fprintf(fp,"\n");
from=colour_com_strip(from);
fprintf(fp,"From: %s %s\n",from,long_date(0));
message=colour_com_strip(message);
fputs(message,fp);
fputs("\n\n",fp);
fputs(talker_signature,fp);
fclose(fp);
send_forward_email(u->email,filename);
sprintf(text,"%s had mail sent to their email address.\n",u->name);
write_syslog(text,1);
if (!on) {
  destruct_user(u);
  destructed=0;
  }
return;
}

show_attributes(user)
UR_OBJECT user;
{
char *onoff[]={"Off","On"};
char *shide[]={"Showing","Hidden"};

write_user(user,"+----------------------------------------------------------------------------+\n");
write_user(user,"| ~FT~OLStatus of your set attributes~RS                                              |\n");
write_user(user,"+----------------------------------------------------------------------------+\n");
      sprintf(text," Age              : %d\n",user->age);
      write_user(user,text);
      sprintf(text," E-Mail           : %s\n",user->email);
      write_user(user,text);
      sprintf(text," Forwarding Smail : ~OL%s~RS\n",onoff[user->autofwd]);
      write_user(user,text);
      sprintf(text," Gender           : ~OL%s~RS\n",genders[user->gender]);
      write_user(user,text);
      sprintf(text," Hiding E-Mail    : ~OL%s~RS\n",shide[user->hidemail]);
      write_user(user,text);
      sprintf(text," ICQ              : %s\n",user->icq);
      write_user(user,text);
      sprintf(text," URL              : %s\n",user->url);
      write_user(user,text);
write_user(user,"+----------------------------------------------------------------------------+\n");
write_user(user,"For more help on the .set command, you can try ~OL.help set~RS.\n");
}

/** Show command, ie 'Type --> <command>' **/ 
show(user,inpstr) 
UR_OBJECT user;
char *inpstr;
{ 
  if (word_count<2 && inpstr[1]<33) {
  write_usage(user,"show <command>\n");  return;
  }
if (user->muzzled) {
  write_user(user,"You are currently muzzled and cannot broadcast.\n");
  return;
  }
sprintf(text,"~FT~OLType -->~RS %s\n",inpstr);
write_room(user->room,text);
}

/* clears a room topic */
clear_topic(user)
UR_OBJECT user;
{
RM_OBJECT rm;
char *name;

strtolower(word[1]);
if (word_count<2) {
  rm=user->room;
  rm->topic[0]='\0';
  write_user(user,"Topic has been cleared.\n");
  if (user->vis) name=user->name; else name=invisname;
  sprintf(text,"~FY~OL%s has cleared the topic.~RS\n",name);
  write_room_except(rm,text,user);
  sprintf(text,"[ %s(%s) has cleared the topic in room %s. ]\n",name,levels[user->level],user->room->name);
  write_level(ARCH,1,text,NULL);
  return;
  }
if (!strcmp(word[1],"all")) {
  if (user->level>=ARCH) {
    for(rm=room_first;rm!=NULL;rm=rm->next) {
      rm->topic[0]='\0';
      write_room_except(rm,"\n~FY~OLThe topic has been cleared.\n",user);
      }
    write_user(user,"All room topics have now been cleared.\n");
    write_level(ARCH,1,"[ All room topics have now been cleared. ]\n",NULL);
    return;
    }
  write_user(user,"You can only clear the topic of the room you are in.\n");
  return;
  }
write_usage(user,"ctopic [all]\n");
}

/*** Show list of people board posts are from without seeing the whole lot ***/
board_from(user)
UR_OBJECT user;
{
FILE *fp;
int cnt;
char id[ARR_SIZE],line[ARR_SIZE],filename[80],rmname[USER_NAME_LEN];
RM_OBJECT rm;

if (word_count<2) rm=user->room;
else {
  if ((rm=get_room(word[1]))==NULL) {
    write_user(user,nosuchroom);  return;
    }
  if (!has_room_access(user,rm)) {
    write_user(user,"That room is currently private, you cannot read the board.\n");
    return;
    }
  }	
if (!rm->mesg_cnt) {
  write_user(user,"That room has no messages on its board.\n");
  return;
  }
sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
if (!(fp=fopen(filename,"r"))) {
  write_user(user,"There was an error trying to read message board.\n");
  sprintf(text,"Unable to open message board for %s in board_from().\n",rm->name);
  write_syslog(text,0);
  return;
  }
sprintf(text,"\n~FG~BB*** Posts on the %s message board from ***\n\n",rm->name);
write_user(user,text);
cnt=0;  line[0]='\0';
fgets(line,ARR_SIZE-1,fp);
while(!feof(fp)) {
  sscanf(line,"%s",id);
  if (!strcmp(id,"PT:")) {
    cnt++;
    sprintf(text,"~FT%2d)~RS %s",cnt,remove_first(remove_first(remove_first(line))));
    write_user(user,text);
    }
  line[0]='\0';
  fgets(line,ARR_SIZE-1,fp);
  }
fclose(fp);
sprintf(text,"\nTotal of ~OL%d~RS messages.\n\n",rm->mesg_cnt);
write_user(user,text);
}

/** Tell something to everyone but one person **/
mutter(user,inpstr)
UR_OBJECT user; 
char *inpstr;
{
UR_OBJECT u;
char *name;

if (word_count<3) {
  write_user(user,"Usage: mutter <name> <message>\n");
  return;
  }
if (!(u=get_user(word[1]))) {
	write_user(user,notloggedon);  return;
	}
if (!u->vis) {                              
	write_user(user,notloggedon);
        return;                             
        }
inpstr=remove_first(inpstr);
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	}
if (user->room!=u->room) {
  sprintf(text,"%s~RS is not in the same room, so speak freely of them.\n",u->name);
  write_user(user,text);
  return;
  }
if (u==user) {
  write_user(user,"Talking about yourself is a sign of madness!\n");
  return;
  }
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"~FT%s mutters:~RS %s ~FY~OL(to all but %s)~RS\n",name,inpstr,u->name);
write_room_except(user->room,text,u);
}

/*** List all users who are AFK ***/
listafks(user)
UR_OBJECT user;
{
   UR_OBJECT u;
   int afks=0;
   write_user(user,"~FB~OL--->>> AFK users <<<---\n\n");
   for(u=user_first;u!=NULL;u=u->next)
     {
        if(!u->afk || (!u->vis && u->level>user->level))
          continue;
        sprintf(text,"   %-16s : { ~FT%s~RS } - idle for ~FT%d~RS minutes\n",
          u->name,u->afk_mesg,(int)(time(0) - u->last_input)/60);
        write_user(user,text);
        afks++;
     }
   if(afks)
      {
        sprintf(text,"\nTotal of ~FT%d~RS AFK users.\n",afks);
        write_user(user,text);
      }  
   else
     write_user(user,"There are no AFK users.\n");
}

/*** Puzzle game interface ***/
puzzle_com(user)
UR_OBJECT user;
{
  int i;
  char moves[]="lrfb";
  
  if(word_count<2)
    {
      write_usage(user,"puzzle ?/start/stop/status/<direction>\n");
      return;
    }
  if(!strcmp(word[1],"?") || !strcmp(word[1],"status"))
    {
      puzzle_status(user,"\n~BG~OL--->>> Puzzle status <<<---\n\n");
      return;
    }
  if(!strcmp(word[1],"start"))
    {
      puzzle_start(user);
      return;
    }      
  if(!strcmp(word[1],"stop"))
    {
      puzzle_stop(user);
      return;
    }    
  for(i=0;i<4;i++)
    if(moves[i]==word[1][0])
      {
        puzzle_play(user,i);
        return;
      }
  write_usage(user,"puzzle l/r/f/b\n");      
}

/*** Puzzle swap free position on table ***/
puzzle_swap(user,oldlin,oldcol,newlin,newcol)
UR_OBJECT user;
int oldlin,oldcol,newlin,newcol;
{
  int aux;
  aux=user->puzzle->table[oldlin][oldcol];
  user->puzzle->table[oldlin][oldcol]=user->puzzle->table[newlin][newcol];
  user->puzzle->table[newlin][newcol]=aux;
}
                  
/*** Puzzle status ***/
puzzle_status(user,msg)
UR_OBJECT user;
char *msg;
{
  int i,j,k;
  if(user->puzzle==NULL)
    {
      write_user(user,"You are not playing the puzzle right now.\n");
      return;
    }
  write_user(user,msg);
  for(i=0;i<GM_TDIM;i++)
    {
      strcpy(text,"~BT -");
      for(k=0;k<GM_TDIM*3;k++)
        strcat(text,"-");
      write_user(user,text);
      write_user(user,"\n~BT |");
      for(j=0;j<GM_TDIM;j++)
        {
          if(user->puzzle->table[i][j]!=-1)
            sprintf(text,"~OL%2d~RS~BT|",user->puzzle->table[i][j]);
          else
            strcpy(text,"~OL  ~RS~BT|");  
          write_user(user,text);
        }
      write_user(user,"\n");
    }
  strcpy(text,"~BT -");
  for(k=0;k<GM_TDIM*3;k++)
    strcat(text,"-");
  write_user(user,text);          
  write_user(user,"\n\n");
}    

/*** Puzzle start a game ***/
puzzle_start(user)
UR_OBJECT user;
{
  int i,j,c;
  if(user->puzzle!=NULL)
    {
      write_user(user,">>>You must stop your current game, then use this command.\n");
      return;
    }
  user->puzzle=(struct puzzle_struct*)malloc(sizeof(struct puzzle_struct));
  if(user->puzzle==NULL)
    {
      write_user(user,"No memory to play puzzle, sorry...\n");
      write_syslog("Couldn't allocate memory in puzzle_start().\n",0);
      return;
    }
  for(i=0;i<GM_TDIM;i++)		/* fill puzzle table */ 
    for(j=0;j<GM_TDIM;j++)
      user->puzzle->table[i][j]=-1;     /* -1 means free */   
  for(c=1;c<GM_TDIM*GM_TDIM;c++)	/* randomly, fill with numbers */
    {
      do {
        i=random()%GM_TDIM;
        j=random()%GM_TDIM;
      }
      while(user->puzzle->table[i][j]!=-1);
      user->puzzle->table[i][j]=c;
    }
  for(i=0;i<GM_TDIM;i++)		/* get free position on table */
    for(j=0;j<GM_TDIM;j++)
      if(user->puzzle->table[i][j]==-1) /* the position is free */
        {
          user->puzzle->clin=i;
          user->puzzle->ccol=j;
          break;
        }
  puzzle_status(user,"\n~BG~OL--->>> Starting Puzzle... <<<---\n\n");
}

/*** Puzzle stop game ***/
puzzle_stop(user)
UR_OBJECT user;
{
  if(user->puzzle==NULL)
    {
      write_user(user,"You aren't playing the puzzle!\n");
      return;
    }
  free(user->puzzle);
  user->puzzle=NULL;
  write_user(user,"Puzzle stopped.\n");
}

/*** Puzzle play interface ***/
puzzle_play(user,dir)
UR_OBJECT user;
int dir;
{
  int i,j,err=0,ok=0;
  if(user->puzzle==NULL)
    {
      write_user(user,"You aren't playing puzzle at this time...\nUse .puzzle start to play a new game.\n");
      return;
    }
  switch(dir) 
    {
      case GM_LT: if(user->puzzle->ccol==0)
      		    err=1;
      		  else
      		    {
      		      puzzle_swap(user,user->puzzle->clin,user->puzzle->ccol,user->puzzle->clin,user->puzzle->ccol-1);
      		      user->puzzle->ccol--;
      		    }
      		  break;  	
      case GM_RG: if(user->puzzle->ccol==GM_TDIM-1)
      		    err=1;
      		  else
      		    {
      		      puzzle_swap(user,user->puzzle->clin,user->puzzle->ccol,user->puzzle->clin,user->puzzle->ccol+1);
		      user->puzzle->ccol++;
		    }        		    
      		  break;  	      		  
      case GM_FD: if(user->puzzle->clin==0)
      		    err=1;
      		  else
      		    {
      		      puzzle_swap(user,user->puzzle->clin,user->puzzle->ccol,user->puzzle->clin-1,user->puzzle->ccol);
      		      user->puzzle->clin--;
      		    }  
      		  break;  	      		  
      case GM_BK: if(user->puzzle->clin==GM_TDIM-1)
      		    err=1;
      		  else
      		    {
      		      puzzle_swap(user,user->puzzle->clin,user->puzzle->ccol,user->puzzle->clin+1,user->puzzle->ccol);
      		      user->puzzle->clin++;
      		    }  
    }
  if(err)
    puzzle_status(user,"\n~BG~OL--->>> Puzzle - illegal move! <<<---\n\n");
  else
    {
      for(i=0;i<GM_TDIM;i++)		/* check if game is over */
        for(j=0;j<GM_TDIM;j++)
          {
            if(user->puzzle->table[i][j]==-1) continue;
            ok=user->puzzle->table[i][j]==i*GM_TDIM+j+1;
          }  
      if(ok)        
        {
          puzzle_status(user,"\n~BG~OL--->>> Puzzle - Congratulations! <<<---\n\n");      
          puzzle_stop(user);
        }
      else    
        puzzle_status(user,"\n~BG~OL--->>> Puzzle - current status <<<---\n\n");  
    }    
}

ranks(user)
UR_OBJECT user;
{
char buf[63536]="";
	strcat(buf,"\n");
	write_user(user,"\n********************************************************************************\n");
	/*strcat(buf,text);*/
	sprintf(text, "	          The following are the ranks on %s\n\n\r",talkername);
	write_user(user,text);
	write_user(user,"~OLDunce~RS                        ~OLArrested~RS\n"
	"~OLUser~RS                         ~OLA regular user~RS\n"
	"~OLJunior Wizard~RS                ~OLA Wizard in training~RS\n"
	"~OLWizard~RS                       ~OLSelf-explanatory~RS\n"
	"~OLHonor~RS                        ~OLUsually an admin of another talker but can also be~RS\n"
        "                             ~OLadvisor types.~RS\n"
        "~OLAdmin~RS                        ~OLAdministrators or coders.~RS\n"
        "~OLGod~RS                          ~OLSuper-user.~RS\n"
        "~OLCreator~RS                      ~OLCreator of the talker. Unachievable position.~RS\n");    /*,clrs[y]);*/
/*	strcat(buf, text);*/
        write_user(user,
"********************************************************************************\n");
/*        strcat(buf,text);*/
	strcat(buf,"\n");
	write_user(user,buf);
}

/* Shift and Shout-Shift: mAkEs tExT LoOk lIkE ThIs */
/* from TalkerOS                                    */
char_shift(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
int i,j;
i=0;
j=0;
if (user->muzzled) {
        write_user(user,"-=- You are muzzled... Action denied. -=-\n");  return;
        }
if (word_count<2) {
        write_user(user,"Shift what?\n");  return;
        }
if (ban_swearing && contains_swearing(inpstr)) {
        write_user(user,noswearing);  return;
        }
for (i=0; i < strlen(inpstr); i++) {
        if (j++ % 2) { inpstr[i]=tolower(inpstr[i]); }
        else { inpstr[i]=toupper(inpstr[i]); }
        }
say(user,inpstr);
}

/*** Shift Shout something ***/
sshout(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
int i,j;
char *name;
i=0;
j=0;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot shout.\n");  return;
	}
if (user->ignshout) {
	write_user(user,"You are currently ignoring shouts and shout emotes.\n");  return;
	}
if (word_count<2) {
	write_user(user,"Shout what?\n");  return;
	}
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	}
	
for (i=0; i < strlen(inpstr); i++) {
        if (j++ % 2) { inpstr[i]=tolower(inpstr[i]); }
        else { inpstr[i]=toupper(inpstr[i]); }
        }
sprintf(text,"~OLYou shift shout:~RS %s\n",inpstr);
write_user(user,text);
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"~OL%s shouts:~RS %s\n",name,inpstr);
write_room_except(NULL,text,user);
}

/*  This will rename a user to a new name.  Isn't that nice?  The user
    must be logged in at the time, because if you changed it while they
    were not logged in, how would they know that you changed it?!  This
    simplifies having to go into the shell to rename all the files, and
    it also lets you give your high-ranking wizards the ability to do
    this without them having to bug you, or having to give them shell
    access. */

user_rename_files(user,str)
UR_OBJECT user;
char *str;
{
UR_OBJECT u,u2;
char oldfile[81],newfile[81];
int i,len;

if (word_count<3) { write_user(user,"Usage:  .urename <user> <newName>\n"); return; }
/* make sure we aren't renaming someone to a swear word... of course
   if the swear ban is off, this won't matter. */
if (contains_swearing(str)) {
        write_user(user,"Swear words can not be used in the user name.\n");
        return;
        }
/* check to see if the user is logged in or not (see above) */
if (!(u=get_user(word[1]))) {
        write_user(user,notloggedon);
        return;
        }
/* user is logged in */
else {
        /* make sure the user isn't temp-promoted, and that he/she isn't
           a lower rank than the person he/she is changing. */
        if (u->level >= user->level) {
                write_user(user,"File access denied.\n");
                sprintf(text,"[ %s attempted to RENAME user: %s ]\n",user->name,u->name);
		write_level(ARCH,1,text,NULL);
                return;
                }                
        /* make sure the user to be renamed is renameable  :)  */
        if (u->type!=USER_TYPE) { write_user(user,"ERROR:  User is not the correct type and cannot be renamed.\n"); return; }
        /* make new user struct */
        if ((u2=create_user())==NULL) {
                write_user(user,"ERROR:  Could not create new user object in user_rename_files()!\n");
                write_syslog("ERROR:  Couldn't create new user object in user_rename_files().\n",0,SYSLOG);
                return;
                }
        /* check validity of new name and do CAPS stuff */
        for (i=0;i<strlen(word[2]);++i) {
                if (!isalpha(word[2][i])) {
                        write_user(user,"\n-=- Only letters are allowed in a username. -=-\n\n");
                        destruct_user(u2); destructed=0;
                        return;
                        }
                if (!i) word[2][0] = toupper(word[2][0]);
                else if (!allow_caps_in_name) word[2][i] = tolower(word[2][i]);
                }
        /* check to see if user exists with new name */
        strcpy(u2->name,word[2]);
        if (load_user_details(u2)) {
                write_user(user,"ERROR:  Cannot rename, user exists with new name.\n");
                destruct_user(u2); destructed=0;
                return;
                }
        /* Do file stuff */
        sprintf(oldfile,"%s/%s.D",USERFILES,u->name);
        sprintf(newfile,"%s/%s.D",USERFILES,u2->name);
        rename(oldfile,newfile);
        unlink(oldfile);
        sprintf(oldfile,"%s/%s.P",USERFILES,u->name);
        sprintf(newfile,"%s/%s.P",USERFILES,u2->name);
        rename(oldfile,newfile);
        unlink(oldfile);
        sprintf(oldfile,"%s/%s.M",USERFILES,u->name);
        sprintf(newfile,"%s/%s.M",USERFILES,u2->name);
        rename(oldfile,newfile);
        unlink(oldfile);
        save_user_details(u2,1);
        /* End file stuff */

        /* Do announcements */
        sprintf(text,"[ ~OLSYSTEM:~RS  %s(%s) has been renamed to: %s! ]\n",u->name,levels[u->level],u2->name);
        write_room_except(NULL,text,u);
        sprintf(text,"[ %s(%s) RENAMED user '%s' to '%s' ]\n",user->name,levels[user->level],u->name,u2->name);
	write_level(ARCH,1,text,NULL);
        sprintf(text,"%s RENAMED user '%s' to '%s'\n",user->name,u->name,u2->name);
        write_syslog(text,0);
        sprintf(text,"\n~FR~OLATTENTION:  You have been renamed to: %s\n            This is now your NEW username.  The password remains unchanged.\n\n",u2->name);
        write_user(u,text);

        /* Change the actual user name */
        strcpy(u->name,u2->name);

        /* clean up */
        destruct_user(u2);  destructed=0;

        return;
        }

/* All that just to rename a user... I would have to rename 60 people in
the shell to equal that much typing.  Oh well, i guess its convenient for
you. */
}

set_user_total_time(user)
UR_OBJECT user;
{
UR_OBJECT u;
int days,hours,mins,total;
int days2,hours2,mins2;
char plural[2][2]={"s",""};

if (word_count<5) { write_user(user,"Usage:  .settime <user> <days> <hours> <minutes>\n"); return; }
if (get_user(word[1])) { write_user(user,"-=- You cannot alter the total time of a user currently logged in. -=-\n"); return; }
if (!isnumber(word[2])) { write_user(user,"The number of days must be a number.\n"); return; }
if (!isnumber(word[3])) { write_user(user,"The number of hours must be a number.\n"); return; }
if (!isnumber(word[4])) { write_user(user,"The number of minutes must be a number.\n"); return; }
if ((u=create_user())==NULL) {
        sprintf(text,"%s: unable to create temporary user object.\n",syserror);
        write_user(user,text);
        write_syslog("ERROR: Unable to create temporary user object in set_user_total_time().\n",0,SYSLOG);
        return;
        }
strcpy(u->name,word[1]);
if (!(load_user_details(u))) { write_user(user,nosuchuser); destruct_user(u); destructed=0; return; }
if (u->level>=user->level) { write_user(user,"You cannot change the total time of a user equal to or higher than you in rank.\n"); destruct_user(u); destructed=0; return; }

days=atoi(word[2]);
hours=atoi(word[3]);
mins=atoi(word[4]);
days2=u->total_login/86400;
hours2=(u->total_login%86400)/3600;
mins2=(u->total_login%3600)/60;

sprintf(text," Time Entered:  %d day%s, %d hour%s, %d minute%s.\n",days,plural[(days==1)],hours,plural[(hours==1)],mins,plural[(mins==1)]);
write_user(user,text);
total=days*86400;
total=total+(hours*3600);
total=total+(mins*60);
u->total_login=total;
sprintf(text,"Result Stored:  %d\n",total);
write_user(user,text);
days=u->total_login/86400;
hours=(u->total_login%86400)/3600;
mins=(u->total_login%3600)/60;
sprintf(text,"     New Time:  %d day%s, %d hour%s, %d minute%s.\n",days,plural[(days==1)],hours,plural[(hours==1)],mins,plural[(mins==1)]);
write_user(user,text);
save_user_details(u,0);
sprintf(text,"%s altered %s's total time.\n",user->name,u->name);
write_syslog(text,0);
sprintf(text,"\nNOTICE:\nYour total time has been changed to:\n     %d day%s, %d hour%s, and %d minute%s.\n\nIt was originally:\n     %d day%s, %d hour%s, and %d minute%s.\n",days,plural[(days==1)],hours,plural[(hours==1)],mins,plural[(mins==1)],days2,plural[(days2==1)],hours2,plural[(hours2==1)],mins2,plural[(mins2==1)]);
send_mail(user,u->name,text);
destruct_user(u);
destructed=0;
write_user(user,"-=- Saved. -=-\n");
}

/*
 *   Simple seamless reboot for Amnuts by Ardant (ardant@ardant.net).
 *   Credits stay with the code. Special thanks to Phypor and Arny.
 */


/* Installation

   Now, under the comment:

   finish off the boot-up process

   put the line:

   if (argc>2) if (atoi(argv[2])>1) do_srload();

   Now, make a command like normal, calling the function do_srboot();
   
   Make a reboot directory in your home directory

   You're done! Enjoy!

*/

void do_srboot()

{

  UR_OBJECT u,next;
  FILE *fp,*fpi;
  char filename[80];
  char *args[]={ progname,confile,"3",NULL};
  int i;
  write_room(NULL,"~FT~OLA seamless reboot hits the land...\n");
  u=user_first;
  fp=fopen("userlist","w");
  while (u) {
    next=u->next;
    if (u->type==USER_TYPE) {
      fprintf(fp,"%s %s\n",u->name,u->room->name);
      sprintf(filename,"reboot/%s",u->name);
      if (fpi=fopen(filename,"w")) {
        fwrite(u,sizeof(struct user_struct),1,fpi);
        unlink(filename);
        fclose(fpi);
      }
    }
    destruct_user(u);
    u=next;
  }
  fclose(fp);
  for (i=0;i<3;i++) close(listen_sock[i]);
  execvp(progname,args);
  exit(0);
}

void do_srload()
{
FILE *fp,*fpi;
char name[40],rname[40],filename[80];
UR_OBJECT u,p,n;
if (!(fp=fopen("userlist","r"))) return;
fscanf(fp,"%s %s",name,rname);
while (!feof(fp)) {
  u=create_user();
  strcpy(u->name,name);
  sprintf(filename,"reboot/%s",u->name);
  if (fpi=fopen(filename,"r")) {
    p=u->prev; n=u->next;
    fread(u,sizeof(struct user_struct),1,fpi);
    u->prev=p; u->next=n;
    if (!(u->room = get_room(rname))) u->room=room_first;
    fclose(fpi);
  } else {
    destruct_user(u);
  }
  fscanf(fp,"%s %s",name,rname);
}
fclose(fp);
unlink("userlist");
}

/*** Show who is on ***/
who(user,people)
UR_OBJECT user;
int people;
{
UR_OBJECT u;
int cnt,total,invis,mins,remote,idle,logins;
char line[USER_NAME_LEN+USER_DESC_LEN*2];
char rname[ROOM_NAME_LEN+1],portstr[5],idlestr[15],sockstr[3];

total=0;  invis=0;  remote=0;  logins=0;
if (user->login) sprintf(text,"\n*** Current users %s ***\n\n",long_date(1));
else sprintf(text,"\n~BB*** Current users %s ***\n\n",long_date(1));
write_user(user,text);
if (people) write_user(user,"~FTName            :  Level   Line Ignall Visi Idle Mins  Port  Site/Service\n\n\r");
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE) continue;
	mins=(int)(time(0) - u->last_login)/60;
	idle=(int)(time(0) - u->last_input)/60;
	if (u->type==REMOTE_TYPE) strcpy(portstr,"   -");
	else {
		if (u->port==port[0]) strcpy(portstr,"MAIN");
		else strcpy(portstr," WIZ");
		}
	if (u->login) {
		if (!people) continue;
		sprintf(text,"~FY[Login stage %d] :     -   %2d      -    - %4d    -  %s  %s:%d\n",4 - u->login,u->socket,idle,portstr,u->site,u->site_port);
		write_user(user,text);
		logins++;
		continue;
		}
	++total;
	if (u->type==REMOTE_TYPE) ++remote;
        if (!u->vis) {
                ++invis;
		if (u->level>user->level) continue;
                }
	if (people) {
    		if (u->afk) strcpy(idlestr," ~FRAFK~RS");
		else if (u->editing) strcpy(idlestr,"~FTEDITING~RS");
		else sprintf(idlestr,"%4d",idle);
	        if (u->type==REMOTE_TYPE) strcpy(sockstr," @");
	    	else sprintf(sockstr,"%2d",u->socket);
    		sprintf(text,"%-15s : %-6s   %s    %s  %s %s %4d  %s  %s\n",u->name,level_name[u->level],sockstr,noyes1[u->ignall],noyes1[u->vis],idlestr,mins,portstr,u->site);
		write_user(user,text);
    		continue;
		}
	if (!u->vis) sprintf(line,"  %s (invis) the %s, %s~RS",u->name,level_name[u->level],u->desc);
	else if (user->level>=JRWIZ) sprintf(line,"  %s the %s, %s~RS",u->name,level_name[u->level],u->desc);
	else sprintf(line,"  %s the %s, %s~RS",u->name,level_name[u->level],u->desc);
	if (u->type==REMOTE_TYPE) line[1]='@';
	if (u->room==NULL) sprintf(rname,"@%s",u->netlink->service);
	else strcpy(rname,u->room->name);
	/* Count number of colour coms to be taken account of when formatting */
	cnt=colour_com_count(line);
	if (u->afk) strcpy(idlestr,"~FRAFK~RS");
	else if (u->editing) strcpy(idlestr,"~FTEDITING~RS");
	else if (idle>=30) strcpy(idlestr,"~FYIDLE~RS");
	else sprintf(idlestr,"%d mins.",mins);
	if ((user->level>=JRWIZ) && (user->level>=u->level)) sprintf(text,"%-*s : %-12s : %s\n",40+cnt*3,line,rname,idlestr);
	else sprintf(text,"%-*s : %s\n",40+cnt*3,line,idlestr);
	write_user(user,text);
	}
sprintf(text,"\nThere are %d visible and %d remote users.\nTotal of %d users",num_of_users,remote,total-invis);
if (people) sprintf(text,"%s and %d logins.\n\n",text,logins);
else strcat(text,".\n\n");
write_user(user,text);
}

/*** Do the help ***/
help(user)
UR_OBJECT user;
{
int ret;
char filename[80];
char *c;

if (word_count<2) {
        help_commands(user);
	if (ret==1) user->misc_op=2;
	return;
	}

if (!strcmp(word[1],"credits")) {  help_credits(user);  return;  }

/* Check for any illegal crap in searched for filename so they cannot list 
   out the /etc/passwd file for instance. */
c=word[1];
while(*c) {
	if (*c=='.' || *c++=='/') {
		write_user(user,"Sorry, there is no help on that topic.\n");
		return;
		}
	}
sprintf(filename,"%s/%s",HELPFILES,word[1]);
if (!(ret=more(user,user->socket,filename)))
	write_user(user,"Sorry, there is no help on that topic.\n");
if (ret==1) user->misc_op=2;
}


/*** Show the command available ***/
help_commands(user)
UR_OBJECT user;
{
int com,cnt,lev,cmds,num;
char temp[20];

sprintf(text,"\n~BB*** Commands available for level: %s ***\n\n",level_name[user->level]);
write_user(user,text);
num=0; cmds=0;
for(lev=DUNCE;lev<=user->level;++lev) {
	sprintf(text,"~OL(%s)~RS\n",level_name[lev]);
	write_user(user,text);
	com=0;  cnt=0;  text[0]='\0';
	while(command[com][0]!='*') {
		if (com_level[com]!=lev) {  com++;  continue;  }
		sprintf(temp,"%-10s ",command[com]);
		strcat(text,temp);
		num++;
		if (cnt==6) {  
			strcat(text,"\n");  
			write_user(user,text);  
			text[0]='\0';  cnt=-1;  
			}
		com++; cnt++;
		}
	if (cnt) {
		strcat(text,"\n");  write_user(user,text);
		}
	}
sprintf(text,"\nThere are ~OL%d~RS commands available in total.\nYou have ~OL%d~RS commands available to you.\n\n",com,num);
write_user(user,text);
write_user(user,"Shortcuts: ~OL#~RS shout    ~OL-~RS echo    ~OL;~RS emote    ~OL<~RS pemote    ~OL>~RS tell    ~OL@~RS who\n");
write_user(user,"           ~OL'~RS show\n");
write_user(user,"Additional topics: ~OLcredits~RS ~OLtalkers~RS\n\n");
write_user(user,"Type '~FG.help <command name>~RS' for specific help on a command.\nRemember, you can use a '.' on its own to repeat your last command or speech.\n\n");
}


/*** Show the credits. Add your own credits here if you wish but PLEASE leave 
     my credits intact. Thanks. */
help_credits(user)
UR_OBJECT user;
{
sprintf(text,"\n~BB*** The Credits :) ***\n\n~BRBolts version %s, by Nathan D Richards.\n",boltsversion);
write_user(user,text);
sprintf(text,"Contributions by Benjamin Beau Perry <bbp0281@ksu.edu>, Scott Rosendahl <cybermanscott@hotmail.com> and\n");
write_user(user,text);
sprintf(text,"Carlos Rosado <thesunsetstrip@hotmail.com>.  Based on NUTS %s by\n",VERSION);
write_user(user,text);
sprintf(text,"Neil Robertson, credits below.\n\n");
write_user(user,text);
sprintf(text,"NUTS version %s, Copyright (C) Neil Robertson 1996.\n\n",VERSION);
write_user(user,text);
write_user(user,"~BM             ~BB             ~BT             ~BG             ~BY             ~BR             ~RS\n");
write_user(user,"NUTS stands for Neils Unix Talk Server, a program which started out as a\nuniversity project in autumn 1992 and has progressed from thereon. In no\nparticular order thanks go to the following people who helped me develop or\n");
write_user(user,"debug this code in one way or another over the years:\n   ~FTDarren Seryck, Steve Guest, Dave Temple, Satish Bedi, Tim Bernhardt,\n   ~FTKien Tran, Jesse Walton, Pak Chan, Scott MacKenzie and Bryan McPhail.\n"); 
write_user(user,"Also thanks must go to anyone else who has emailed me with ideas and/or bug\nreports and all the people who have used NUTS over the intervening years.\n");
write_user(user,"I know I've said this before but this time I really mean it - this is the final\nversion of NUTS 3. In a few years NUTS 4 may spring forth but in the meantime\nthat, as they say, is that. :)\n\n");
write_user(user,"If you wish to email me my address is '~FGneil@ogham.demon.co.uk~RS' and should\nremain so for the forseeable future.\n\nNeil Robertson - November 1996.\n");
write_user(user,"~BM             ~BB             ~BT             ~BG             ~BY             ~BR             ~RS\n\n");
}


/*** Read the message board ***/
read_board(user)
UR_OBJECT user;
{
RM_OBJECT rm;
char filename[80],*name;
int ret;

if (word_count<2) rm=user->room;
else {
	if ((rm=get_room(word[1]))==NULL) {
		write_user(user,nosuchroom);  return;
		}
	if (!has_room_access(user,rm)) {
		write_user(user,"That room is currently private, you cannot read the board.\n");
		return;
		}
	}	
sprintf(text,"\n~BB*** The %s message board ***\n\n",rm->name);
write_user(user,text);
sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
if (!(ret=more(user,user->socket,filename))) 
	write_user(user,"There are no messages on the board.\n\n");
else if (ret==1) user->misc_op=2;
if (user->vis) name=user->name; else name=invisname;
if (rm==user->room) {
	sprintf(text,"%s reads the message board.\n",name);
	write_room_except(user->room,text,user);
	}
}


/*** Write on the message board ***/
write_board(user,inpstr,done_editing)
UR_OBJECT user;
char *inpstr;
int done_editing;
{
FILE *fp;
int cnt,inp;
char *ptr,filename[80];

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot write on the board.\n");  
	return;
	}
if (!done_editing) {
	if (word_count<2) {
		if (user->type==REMOTE_TYPE) {
			/* Editor won't work over netlink cos all the prompts will go
			   wrong, I'll address this in a later version. */
			write_user(user,"Sorry, due to software limitations remote users cannot use the line editor.\nUse the '.write <mesg>' method instead.\n");
			return;
			}
		write_user(user,"\n~BB*** Writing board message ***\n\n");
		user->misc_op=3;
		editor(user,NULL);
		return;
		}
	ptr=inpstr;
	inp=1;
	}
else {
	ptr=user->malloc_start;  inp=0;
	}

sprintf(filename,"%s/%s.B",DATAFILES,user->room->name);
if (!(fp=fopen(filename,"a"))) {
	sprintf(text,"%s: cannot write to file.\n",syserror);
	write_user(user,text);
	sprintf(text,"ERROR: Couldn't open file %s to append in write_board().\n",filename);
	write_syslog(text,0);
	return;
	}
/* The posting time (PT) is the time its written in machine readable form, this 
   makes it easy for this program to check the age of each message and delete 
   as appropriate in check_messages() */
if (user->type==REMOTE_TYPE) 
	sprintf(text,"PT: %d\r~OLFrom: %s@%s  %s\n",(int)(time(0)),user->name,user->netlink->service,long_date(0));
else sprintf(text,"PT: %d\r~OLFrom: %s  %s\n",(int)(time(0)),user->name,long_date(0));
fputs(text,fp);
cnt=0;
while(*ptr!='\0') {
	putc(*ptr,fp);
	if (*ptr=='\n') cnt=0; else ++cnt;
	if (cnt==80) { putc('\n',fp); cnt=0; }
	++ptr;
	}
if (inp) fputs("\n\n",fp); else putc('\n',fp);
fclose(fp);
write_user(user,"You write the message on the board.\n");
sprintf(text,"%s writes a message on the board.\n",user->name);
write_room_except(user->room,text,user);
user->room->mesg_cnt++;
sprintf(text,"[ %s(%s) has written a message on the %s board. ]\n",user->name,levels[user->level],user->room);
write_level(ARCH,1,text,NULL);
}



/*** Wipe some messages off the board ***/
wipe_board(user)
UR_OBJECT user;
{
int num,cnt,valid;
char infile[80],line[82],id[82];
FILE *infp,*outfp;
RM_OBJECT rm;

if (word_count<2 || ((num=atoi(word[1]))<1 && strcmp(word[1],"all"))) {
	write_usage(user,"wipe <number of messages>/all\n");  return;
	}
rm=user->room;
sprintf(infile,"%s/%s.B",DATAFILES,rm->name);
if (!(infp=fopen(infile,"r"))) {
	write_user(user,"The message board is empty.\n");
	return;
	}
if (!strcmp(word[1],"all")) {
	fclose(infp);
	unlink(infile);
	write_user(user,"All messages deleted.\n");
	sprintf(text,"%s wipes the message board.\n",user->name);
	write_room_except(rm,text,user);
	sprintf(text,"%s wiped all messages from the board in the %s.\n",user->name,rm->name);
	write_syslog(text,1);
	sprintf(text,"[ %s(%s) wiped all messages from the board in the %s. ]\n",user->name,levels[user->level],rm->name);
	write_level(ARCH,1,text,NULL);

	rm->mesg_cnt=0;
	return;
	}
if (!(outfp=fopen("tempfile","w"))) {
	sprintf(text,"%s: couldn't open tempfile.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Couldn't open tempfile in wipe_board().\n",0);
	fclose(infp);
	return;
	}
cnt=0; valid=1;
fgets(line,82,infp); /* max of 80+newline+terminator = 82 */
while(!feof(infp)) {
	if (cnt<=num) {
		if (*line=='\n') valid=1;
		sscanf(line,"%s",id);
		if (valid && !strcmp(id,"PT:")) {
			if (++cnt>num) fputs(line,outfp);
			valid=0;
			}
		}
	else fputs(line,outfp);
	fgets(line,82,infp);
	}
fclose(infp);
fclose(outfp);
unlink(infile);
if (cnt<num) {
	unlink("tempfile");
	sprintf(text,"There were only %d messages on the board, all now deleted.\n",cnt);
	write_user(user,text);
	sprintf(text,"%s wipes the message board.\n",user->name);
	write_room_except(rm,text,user);
	sprintf(text,"%s wiped all messages from the board in the %s.\n",user->name,rm->name);
	write_syslog(text,1);
	sprintf(text,"[ %s(%s) wiped all messages from the board in the %s. ]\n",user->name,levels[user->level],rm->name);
	write_level(ARCH,1,text,NULL);
	rm->mesg_cnt=0;
	return;
	}
if (cnt==num) {
	unlink("tempfile"); /* cos it'll be empty anyway */
	write_user(user,"All messages deleted.\n");
	user->room->mesg_cnt=0;
	sprintf(text,"%s wiped all messages from the board in the %s.\n",user->name,rm->name);
	}
else {
	rename("tempfile",infile);
	sprintf(text,"%d messages deleted.\n",num);
	write_user(user,text);
	user->room->mesg_cnt-=num;
	sprintf(text,"%s wiped %d messages from the board in the %s.\n",user->name,num,rm->name);
	}
write_syslog(text,1);
sprintf(text,"%s wipes the message board.\n",user->name);
write_room_except(rm,text,user);
}

	

/*** Search all the boards for the words given in the list. Rooms fixed to
	private will be ignore if the users level is less than gatecrash_level ***/
search_boards(user)
UR_OBJECT user;
{
RM_OBJECT rm;
FILE *fp;
char filename[80],line[82],buff[(MAX_LINES+1)*82],w1[81];
int w,cnt,message,yes,room_given;

if (word_count<2) {
	write_usage(user,"search <word list>\n");  return;
	}
/* Go through rooms */
cnt=0;
for(rm=room_first;rm!=NULL;rm=rm->next) {
	sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
	if (!(fp=fopen(filename,"r"))) continue;
	if (!has_room_access(user,rm)) {  fclose(fp);  continue;  }

	/* Go through file */
	fgets(line,81,fp);
	yes=0;  message=0;  
	room_given=0;  buff[0]='\0';
	while(!feof(fp)) {
		if (*line=='\n') {
			if (yes) {  strcat(buff,"\n");  write_user(user,buff);  }
			message=0;  yes=0;  buff[0]='\0';
			}
		if (!message) {
			w1[0]='\0';  
			sscanf(line,"%s",w1);
			if (!strcmp(w1,"PT:")) {  
				message=1;  
				strcpy(buff,remove_first(remove_first(line)));
				}
			}
		else strcat(buff,line);
		for(w=1;w<word_count;++w) {
			if (!yes && strstr(line,word[w])) {  
				if (!room_given) {
					sprintf(text,"~BB*** %s ***\n\n",rm->name);
					write_user(user,text);
					room_given=1;
					}
				yes=1;  cnt++;  
				}
			}
		fgets(line,81,fp);
		}
	if (yes) {  strcat(buff,"\n");  write_user(user,buff);  }
	fclose(fp);
	}
if (cnt) {
	sprintf(text,"Total of %d matching messages.\n\n",cnt);
	write_user(user,text);
	}
else write_user(user,"No occurences found.\n");
}



/*** See review of conversation ***/
review(user)
UR_OBJECT user;
{
RM_OBJECT rm=user->room;
int i,line,cnt;

if (word_count<2) rm=user->room;
else {
	if ((rm=get_room(word[1]))==NULL) {
		write_user(user,nosuchroom);  return;
		}
	if (!has_room_access(user,rm)) {
		write_user(user,"That room is currently private, you cannot review the conversation.\n");
		return;
		}
	}
cnt=0;
for(i=0;i<REVIEW_LINES;++i) {
	line=(rm->revline+i)%REVIEW_LINES;
	if (rm->revbuff[line][0]) {
		cnt++;
		if (cnt==1) {
			sprintf(text,"\n~BB~FGReview buffer for the %s:\n\n",rm->name);
			write_user(user,text);
			}
		write_user(user,rm->revbuff[line]); 
		}
	}
if (!cnt) write_user(user,"Review buffer is empty.\n");
else write_user(user,"\n~BB~FG*** End ***\n\n");
}


/*** Return to home site ***/
home(user)
UR_OBJECT user;
{
if (user->room!=NULL) {
	write_user(user,"You are already on your home system.\n");
	return;
	}
write_user(user,"~FB~OLA strange feeling overwhelms you as you feel you are transported elsewhere...\n");
sprintf(text,"REL %s\n",user->name);
write_sock(user->netlink->socket,text);
sprintf(text,"NETLINK: %s returned from %s.\n",user->name,user->netlink->service);
write_syslog(text,1);
sprintf(text,"[ %s(%s) returned from %s. ]\n",user->name,levels[user->level],user->netlink->service);
write_level(ARCH,1,text,NULL);
user->room=user->netlink->connect_room;
user->netlink=NULL;
if (user->vis) {
	sprintf(text,"%s %s\n",user->name,user->in_phrase);
	write_room_except(user->room,text,user);
	}
else write_room_except(user->room,invisenter,user);
look(user);
}


/*** Show some user stats ***/
status(user)
UR_OBJECT user;
{
UR_OBJECT u;
char ir[ROOM_NAME_LEN+1];
int days,hours,mins,hs;

if (word_count<2 || user->level<JRWIZ) {
	u=user;
	write_user(user,"\n~BB*** Your status ***\n\n");
	}
else {
	if (!(u=get_user(word[1]))) {
		write_user(user,notloggedon);  return;
		}
	if (u->level>user->level) {
		write_user(user,"You cannot stat a user of a higher level than yourself.\n");
		return;
		}
	sprintf(text,"\n~BB*** %s's status ***\n\n",u->name);
	write_user(user,text);
	}
if (u->invite_room==NULL) strcpy(ir,"<nowhere>");
else strcpy(ir,u->invite_room->name);
sprintf(text,"Level       : %s\nIgnoring all: %s\n",level_name[u->level],noyes2[u->ignall]);
write_user(user,text);
sprintf(text,"Ign. shouts : %s\nIgn. tells  : %s\n",noyes2[u->ignshout],noyes2[u->igntell]);
write_user(user,text);
sprintf(text,"Ign. chat   : %s\n",noyes2[u->ignchat]);
write_user(user,text);
if (u->type==REMOTE_TYPE || u->room==NULL) hs=0; else hs=1;
sprintf(text,"On home site: %s\nVisible     : %s\n",noyes2[hs],noyes2[u->vis]);
write_user(user,text);
sprintf(text,"Muzzled     : %s\nUnread mail : %s\n",noyes2[(u->muzzled>0)],noyes2[has_unread_mail(u)]);
write_user(user,text);
sprintf(text,"Char echo   : %s\nColour      : %s\nInvited to  : %s\n",offon[u->charmode_echo],offon[u->colour],ir);
write_user(user,text);
sprintf(text,"Description : %s\nIn phrase   : %s\nOut phrase  : %s\n",u->desc,u->in_phrase,u->out_phrase);
write_user(user,text);
mins=(int)(time(0) - u->last_login)/60;
sprintf(text,"Online for  : %d minutes\n",mins);
days=u->total_login/86400;
hours=(u->total_login%86400)/3600;
mins=(u->total_login%3600)/60;
sprintf(text,"Total login : %d days, %d hours, %d minutes\n\n",days,hours,mins);
write_user(user,text);
}



/*** Read your mail ***/
rmail(user)
UR_OBJECT user;
{
FILE *infp,*outfp;
int ret;
char c,filename[80],line[DNL+1];

sprintf(filename,"%s/%s.M",USERFILES,user->name);
if (!(infp=fopen(filename,"r"))) {
	write_user(user,"You have no mail.\n");  return;
	}
/* Update last read / new mail received time at head of file */
if (outfp=fopen("tempfile","w")) {
	fprintf(outfp,"%d\r",(int)(time(0)));
	/* skip first line of mail file */
	fgets(line,DNL,infp);

	/* Copy rest of file */
	c=getc(infp);
	while(!feof(infp)) {  putc(c,outfp);  c=getc(infp);  }

	fclose(outfp);
	rename("tempfile",filename);
	}
user->read_mail=time(0);
fclose(infp);
write_user(user,"\n~BBYour mail:\n\n");
ret=more(user,user->socket,filename);
if (ret==1) user->misc_op=2;
}



/*** Send mail message ***/
smail(user,inpstr,done_editing)
UR_OBJECT user;
char *inpstr;
int done_editing;
{
UR_OBJECT u;
FILE *fp;
int remote,has_account;
char *c,filename[80];

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot mail anyone.\n");  return;
	}
if (done_editing) {
	send_mail(user,user->mail_to,user->malloc_start);
	user->mail_to[0]='\0';
	return;
	}
if (word_count<2) {
	write_user(user,"Smail who?\n");  return;
	}
/* See if its to another site */
remote=0;
has_account=0;
c=word[1];
while(*c) {
	if (*c=='@') {  
		if (c==word[1]) {
			write_user(user,"Users name missing before @ sign.\n");  
			return;
			}
		remote=1;  break;  
		}
	++c;
	}
word[1][0]=toupper(word[1][0]);
/* See if user exists */
if (!remote) {
	u=NULL;
	if (!(u=get_user(word[1]))) {
		sprintf(filename,"%s/%s.D",USERFILES,word[1]);
		if (!(fp=fopen(filename,"r"))) {
			write_user(user,nosuchuser);  return;
			}
		has_account=1;
		fclose(fp);
		}
	if (u==user) {
		write_user(user,"Trying to mail yourself is the fifth sign of madness.\n");
		return;
		}
	if (u!=NULL) strcpy(word[1],u->name); 
	if (!has_account) {
		/* See if user has local account */
		sprintf(filename,"%s/%s.D",USERFILES,word[1]);
		if (!(fp=fopen(filename,"r"))) {
			sprintf(text,"%s is a remote user and does not have a local account.\n",u->name);
			write_user(user,text);  
			return;
			}
		fclose(fp);
		}
	}
if (word_count>2) {
	/* `One line mail */
	strcat(inpstr,"\n"); 
	send_mail(user,word[1],remove_first(inpstr));
	return;
	}
if (user->type==REMOTE_TYPE) {
	write_user(user,"Sorry, due to software limitations remote users cannot use the line editor.\nUse the '.smail <user> <mesg>' method instead.\n");
	return;
	}
sprintf(text,"\n~BBWriting mail message to %s:\n\n",word[1]);
write_user(user,text);
user->misc_op=4;
strcpy(user->mail_to,word[1]);
editor(user,NULL);
}


/*** Delete some or all of your mail. A problem here is once we have deleted
     some mail from the file do we mark the file as read? If not we could
     have a situation where the user deletes all his mail but still gets
     the YOU HAVE UNREAD MAIL message on logging on if the idiot forgot to 
     read it first. ***/
dmail(user)
UR_OBJECT user;
{
FILE *infp,*outfp;
int num,valid,cnt;
char filename[80],w1[ARR_SIZE],line[ARR_SIZE];

if (word_count<2 || ((num=atoi(word[1]))<1 && strcmp(word[1],"all"))) {
	write_usage(user,"dmail <number of messages>/all\n");  return;
	}
sprintf(filename,"%s/%s.M",USERFILES,user->name);
if (!(infp=fopen(filename,"r"))) {
	write_user(user,"You have no mail to delete.\n");  return;
	}
if (!strcmp(word[1],"all")) {
	fclose(infp);
	unlink(filename);
	write_user(user,"All mail deleted.\n");
	return;
	}
if (!(outfp=fopen("tempfile","w"))) {
	sprintf(text,"%s: couldn't open tempfile.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Couldn't open tempfile in dmail().\n",0);
	fclose(infp);
	return;
	}
fprintf(outfp,"%d\r",(int)time(0));
user->read_mail=time(0);
cnt=0;  valid=1;
fgets(line,DNL,infp); /* Get header date */
fgets(line,ARR_SIZE-1,infp);
while(!feof(infp)) {
	if (cnt<=num) {
		if (*line=='\n') valid=1;
		sscanf(line,"%s",w1);
		if (valid && (!strcmp(w1,"~OLFrom:") || !strcmp(w1,"From:"))) {
			if (++cnt>num) fputs(line,outfp);
			valid=0;
			}
		}
	else fputs(line,outfp);
	fgets(line,ARR_SIZE-1,infp);
	}
fclose(infp);
fclose(outfp);
unlink(filename);
if (cnt<num) {
	unlink("tempfile");
	sprintf(text,"There were only %d messages in your mailbox, all now deleted.\n",cnt);
	write_user(user,text);
	return;
	}
if (cnt==num) {
	unlink("tempfile"); /* cos it'll be empty anyway */
	write_user(user,"All messages deleted.\n");
	user->room->mesg_cnt=0;
	}
else {
	rename("tempfile",filename);
	sprintf(text,"%d messages deleted.\n",num);
	write_user(user,text);
	}
}


/*** Show list of people your mail is from without seeing the whole lot ***/
mail_from(user)
UR_OBJECT user;
{
FILE *fp;
int valid,cnt;
char w1[ARR_SIZE],line[ARR_SIZE],filename[80];

sprintf(filename,"%s/%s.M",USERFILES,user->name);
if (!(fp=fopen(filename,"r"))) {
	write_user(user,"You have no mail.\n");  return;
	}
write_user(user,"\n~BBMail from:\n\n");
valid=1;  cnt=0;
fgets(line,DNL,fp); 
fgets(line,ARR_SIZE-1,fp);
while(!feof(fp)) {
	if (*line=='\n') valid=1;
	sscanf(line,"%s",w1);
	if (valid && (!strcmp(w1,"~OLFrom:") || !strcmp(w1,"From:"))) {
		write_user(user,remove_first(line));  
		cnt++;  valid=0;
		}
	fgets(line,ARR_SIZE-1,fp);
	}
fclose(fp);
sprintf(text,"\nTotal of %d messages.\n\n",cnt);
write_user(user,text);
}



/*** Enter user profile ***/
enter_profile(user,done_editing)
UR_OBJECT user;
int done_editing;
{
FILE *fp;
char *c,filename[80];

if (!done_editing) {
	write_user(user,"\n~BB*** Writing profile ***\n\n");
	user->misc_op=5;
	editor(user,NULL);
	return;
	}
sprintf(filename,"%s/%s.P",USERFILES,user->name);
if (!(fp=fopen(filename,"w"))) {
	sprintf(text,"%s: couldn't save your profile.\n",syserror);
	write_user(user,text);
	sprintf("ERROR: Couldn't open file %s to write in enter_profile().\n",filename);
	write_syslog(text,0);
	return;
	}
c=user->malloc_start;
while(c!=user->malloc_end) putc(*c++,fp);
fclose(fp);
write_user(user,"Profile stored.\n");
sprintf(text,"[ %s(%s) has changed %s profile. ]\n",user->name,levels[user->level],hisher[user->gender]);
write_level(ARCH,1,text,NULL);
}

/*** Examine a user ***/
examine(user)
UR_OBJECT user;
{
UR_OBJECT u,u2;
FILE *fp;
char filename[80],line[82],*emaila;
int new_mail,days,hours,mins,timelen,days2,hours2,mins2,idle;

if (word_count<2) {
	write_user(user,"Examine who?\n");  return;
	}
if (!(u=get_user(word[1]))) {
	if ((u=create_user())==NULL) {
		sprintf(text,"%s: unable to create temporary user object.\n",syserror);
		write_user(user,text);
		write_syslog("ERROR: Unable to create temporary user object in examine().\n",0);
		return;
		}
	strcpy(u->name,word[1]);
	if (!load_user_details(u)) {
		write_user(user,nosuchuser);   
		destruct_user(u);
		destructed=0;
		return;
		}
		u2=NULL;
	}
else u2=u;

if (u->level>=JRWIZ) sprintf(text,"\n~BB*** %s the %s, %s~RS~BB ***\n\n",u->name,level_name[u->level],u->desc);
else sprintf(text,"\n~BB*** %s %s~RS~BB ***\n\n",u->name,u->desc);
write_user(user,text);
sprintf(filename,"%s/%s.P",USERFILES,u->name);
if (!(fp=fopen(filename,"r"))) write_user(user,"No profile.\n");
else {
	fgets(line,81,fp);
	while(!feof(fp)) {
		write_user(user,line);
		fgets(line,81,fp);
		}
	fclose(fp);
	}
sprintf(filename,"%s/%s.M",USERFILES,u->name);
if (!(fp=fopen(filename,"r"))) new_mail=0;
else {
	fscanf(fp,"%d",&new_mail);
	fclose(fp);
	}

days=u->total_login/86400;
hours=(u->total_login%86400)/3600;
mins=(u->total_login%3600)/60;
timelen=(int)(time(0) - u->last_login);
days2=timelen/86400;
hours2=(timelen%86400)/3600;
mins2=(timelen%3600)/60;

if (u2==NULL) {
	sprintf(text,"\n%s is a %d year old %s.",u->name,u->age,genders[u->gender]);
	write_user(user,text);
	/* E-Mail Stuff */
	if (!strcmp(u->email,"-Unset-")) {
		sprintf(text,"\nE-mail address is currently unset.\n");
		write_user(user,text);
		}
	else {
	    if (!u->hidemail) {
		sprintf(text,"\nE-mail address: %s\n",u->email);
		write_user(user,text);
		}
	    else if (u->hidemail && (user->level>=WIZ || u==user)) {
		sprintf(text,"\nE-mail address: %s ~FB~OL(HIDDEN)~RS\n",u->email);
		write_user(user,text);
		}
	    else {
		sprintf(text,"\nE-mail address: %s (Currently only on view to the Wizzes)\n",u->email);
		write_user(user,text);
		}
	    }
	sprintf(text,"\nICQ        : %s\nURL        : %s\nLevel      : %s\nLast login : %s",u->icq,u->url,level_name[u->level],ctime((time_t*)&(u->last_login)));
	write_user(user,text);
	sprintf(text,"Which was  : %d days, %d hours, %d minutes ago\n",days2,hours2,mins2);
	write_user(user,text);
	sprintf(text,"Was on for : %d hours, %d minutes\nTotal login: %d days, %d hours, %d minutes\n",u->last_login_len/3600,(u->last_login_len%3600)/60,days,hours,mins);
	write_user(user,text);
	if (user->level>=WIZARD) {
		sprintf(text,"Last site  : %s\n",u->last_site);
		write_user(user,text);
		}
	if (new_mail>u->read_mail) {
		sprintf(text,"%s has unread mail.\n",u->name);
		write_user(user,text);
		}
	write_user(user,"\n");
	destruct_user(u);
	destructed=0;
	return;
	}
idle=(int)(time(0) - u->last_input)/60;
sprintf(text,"\n%s is a %d year old %s.",u->name,u->age,genders[u->gender]);
write_user(user,text);
/* E-Mail Stuff */
if (!strcmp(u->email,"-Unset-")) {
	sprintf(text,"\nE-mail address is currently unset.\n");
	write_user(user,text);
	}
else {
    if (!u->hidemail) {
	sprintf(text,"\nE-mail address: %s\n",u->email);
	write_user(user,text);
	}
    else if (u->hidemail && (user->level>=WIZ || u==user)) {
	sprintf(text,"\nE-mail address: %s ~FB~OL(HIDDEN)~RS\n",u->email);
	write_user(user,text);
	}
    else {
	sprintf(text,"\nE-mail address: %s (Currently only on view to the Wizzes)\n",u->email);
	write_user(user,text);
	}
    }
if (user->level>=u->level) sprintf(text,"\nICQ         : %s\nURL         : %s\nLevel       : %s\nIgnoring all: %s\n",u->icq,u->url,level_name[u->level],noyes2[u->ignall]);
else sprintf(text,"\nICQ        : %s\nURL        : %s\nLevel      : %s\nLast login : %s",u->icq,u->url,level_name[u->level],ctime((time_t*)&(u->last_login)));
write_user(user,text);
if (user->level>=u->level) {
	sprintf(text,"On since    : %sOn for      : %d hours, %d minutes\n",ctime((time_t *)&u->last_login),hours2,mins2);
	write_user(user,text);
	}
else {
	sprintf(text,"Which was  : %d days, %d hours, %d minutes ago\n",days2,hours2,mins2);
	write_user(user,text);
 	sprintf(text,"Was on for : %d hours, %d minutes\nTotal login: %d days, %d hours, %d minutes\n",u->last_login_len/3600,(u->last_login_len%3600)/60,days,hours,mins);
	write_user(user,text);
	if (user->level>=WIZARD) {
		sprintf(text,"Last site  : %s\n",u->last_site);
		write_user(user,text);
		}
	}
if (user->level>=u->level && u->afk) {
	sprintf(text,"Idle for    : %d minutes ~BR(AFK)\n",idle);
	write_user(user,text);
	if (u->afk_mesg[0]) {
		sprintf(text,"AFK message : %s\n",u->afk_mesg);
		write_user(user,text);
		}
	}
else if (user->level>=u->level) {
	sprintf(text,"Idle for    : %d minutes\n",idle);
	write_user(user,text);
	sprintf(text,"Total login : %d days, %d hours, %d minutes\n",days,hours,mins);
	write_user(user,text);
	}
if (u->socket==-1) {
	sprintf(text,"Home service: %s\n",u->netlink->service);
	write_user(user,text);
	}
else if (u->vis) {
	if (user->level>=WIZARD) {
		sprintf(text,"Site        : %s:%d\n",u->site,u->site_port);
		write_user(user,text);
		}
	}
if (new_mail>u->read_mail) {
	sprintf(text,"%s has unread mail.\n",u->name);
	write_user(user,text);
	}
write_user(user,"\n");
if (!(u->name == user->name)) {
	if (user->level>=u->level) sprintf(text,"[ %s(%s) has examined you. ]\n",user->name,levels[user->level]);
	else sprintf(text,"[ %s(%s) has examined you. ]\n",user->vis?user->name:invisname,levels[user->level]);
	write_user(u,text);
	}
}


/*** Show talker rooms ***/
rooms(user,show_topics)
UR_OBJECT user;
int show_topics;
{
RM_OBJECT rm;
UR_OBJECT u;
NL_OBJECT nl;
char access[9],stat[9],serv[SERV_NAME_LEN+1];
int cnt;

if (show_topics) 
	write_user(user,"\n~BB*** Rooms data ***\n\n~FTRoom name            : Access  Users  Mesgs  Topic\n\n");
else write_user(user,"\n~BB*** Rooms data ***\n\n~FTRoom name            : Access  Users  Mesgs  Inlink  LStat  Service\n\n");
for(rm=room_first;rm!=NULL;rm=rm->next) {
	if (rm->access & PRIVATE) strcpy(access," ~FRPRIV");
	else strcpy(access,"  ~FGPUB");
	if (rm->access & FIXED) access[0]='*';
	cnt=0;
	for(u=user_first;u!=NULL;u=u->next) 
		if (u->type!=CLONE_TYPE && u->room==rm && u->vis) ++cnt;
	if (show_topics)
		sprintf(text,"%-20s : %9s~RS    %3d    %3d  %s\n",rm->name,access,cnt,rm->mesg_cnt,rm->topic);
	else {
		nl=rm->netlink;  serv[0]='\0';
		if (nl==NULL) {
			if (rm->inlink) strcpy(stat,"~FRDOWN");
			else strcpy(stat,"   -");
			}
		else {
			if (nl->type==UNCONNECTED) strcpy(stat,"~FRDOWN");
				else if (nl->stage==UP) strcpy(stat,"  ~FGUP");
					else strcpy(stat," ~FYVER");
			}
		if (nl!=NULL) strcpy(serv,nl->service);
		sprintf(text,"%-20s : %9s~RS    %3d    %3d     %s   %s~RS  %s\n",rm->name,access,cnt,rm->mesg_cnt,noyes1[rm->inlink],stat,serv);
		}
	write_user(user,text);
	}
write_user(user,"\n");
}


/*** List defined netlinks and their status ***/
netstat(user)
UR_OBJECT user;
{
NL_OBJECT nl;
UR_OBJECT u;
char *allow[]={ "  ?","All"," In","Out" };
char *type[]={ "  -"," In","Out" };
char portstr[6],stat[9],vers[8];
int iu,ou,a;

if (nl_first==NULL) {
	write_user(user,"No remote connections configured.\n");  return;
	}
write_user(user,"\n~BB*** Netlink data & status ***\n\n~FTService name    : Allow Type Status IU OU Version  Site\n\n");
for(nl=nl_first;nl!=NULL;nl=nl->next) {
	iu=0;  ou=0;
	if (nl->stage==UP) {
		for(u=user_first;u!=NULL;u=u->next) {
			if (u->netlink==nl) {
				if (u->type==REMOTE_TYPE)  ++iu;
				if (u->room==NULL) ++ou;
				}
			}
		}
	if (nl->port) sprintf(portstr,"%d",nl->port);  else portstr[0]='\0';
	if (nl->type==UNCONNECTED) {
		strcpy(stat,"~FRDown");  strcpy(vers,"-");
		}
	else {
		if (nl->stage==UP) strcpy(stat,"  ~FGUp");
		else strcpy(stat," ~FYVer");
		if (!nl->ver_major) strcpy(vers,"3.?.?"); /* Pre - 3.2 version */  
		else sprintf(vers,"%d.%d.%d",nl->ver_major,nl->ver_minor,nl->ver_patch);
		}
	/* If link is incoming and remoter vers < 3.2 we have no way of knowing 
	   what the permissions on it are so set to blank */
	if (!nl->ver_major && nl->type==INCOMING && nl->allow!=IN) a=0; 
	else a=nl->allow+1;
	sprintf(text,"%-15s :   %s  %s   %s~RS %2d %2d %7s  %s %s\n",nl->service,allow[a],type[nl->type],stat,iu,ou,vers,nl->site,portstr);
	write_user(user,text);
	}
write_user(user,"\n");
}



/*** Show type of data being received down links (this is usefull when a
     link has hung) ***/
netdata(user)
UR_OBJECT user;
{
NL_OBJECT nl;
char from[80],name[USER_NAME_LEN+1];
int cnt;

cnt=0;
write_user(user,"\n~BBMail receiving status:\n\n");
for(nl=nl_first;nl!=NULL;nl=nl->next) {
	if (nl->type==UNCONNECTED || nl->mailfile==NULL) continue;
	if (++cnt==1) write_user(user,"To              : From                       Last recv.\n\n");
	sprintf(from,"%s@%s",nl->mail_from,nl->service);
	sprintf(text,"%-15s : %-25s  %d seconds ago.\n",nl->mail_to,from,(int)(time(0)-nl->last_recvd));
	write_user(user,text);
	}
if (!cnt) write_user(user,"No mail being received.\n\n");
else write_user(user,"\n");

cnt=0;
write_user(user,"\n~BB*** Message receiving status ***\n\n");
for(nl=nl_first;nl!=NULL;nl=nl->next) {
	if (nl->type==UNCONNECTED || nl->mesg_user==NULL) continue;
	if (++cnt==1) write_user(user,"To              : From             Last recv.\n\n");
	if (nl->mesg_user==(UR_OBJECT)-1) strcpy(name,"<unknown>");
	else strcpy(name,nl->mesg_user->name);
	sprintf(text,"%-15s : %-15s  %d seconds ago.\n",name,nl->service,(time(0)-nl->last_recvd));
	write_user(user,text);
	}
if (!cnt) write_user(user,"No messages being received.\n\n");
else write_user(user,"\n");
}


/*** Connect a netlink. Use the room as the key ***/
connect_netlink(user)
UR_OBJECT user;
{
RM_OBJECT rm;
NL_OBJECT nl;
int ret,tmperr;

if (word_count<2) {
	write_usage(user,"connect <room service is linked to>\n");  return;
	}
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
if ((nl=rm->netlink)==NULL) {
	write_user(user,"That room is not linked to a service.\n");
	return;
	}
if (nl->type!=UNCONNECTED) {
	write_user(user,"That rooms netlink is already up.\n");  return;
	}
write_user(user,"Attempting connect (this may cause a temporary hang)...\n");
sprintf(text,"NETLINK: Connection attempt to %s initiated by %s.\n",nl->service,user->name);
write_syslog(text,1);
errno=0;
if (!(ret=connect_to_site(nl))) {
	write_user(user,"~FGInitial connection made...\n");
	sprintf(text,"NETLINK: Connected to %s (%s %d).\n",nl->service,nl->site,nl->port);
	write_syslog(text,1);
	nl->connect_room=rm;
	return;
	}
tmperr=errno; /* On Linux errno seems to be reset between here and sprintf */
write_user(user,"~FRConnect failed: ");
write_syslog("NETLINK: Connection attempt failed: ",1);
if (ret==1) {
	sprintf(text,"%s.\n",sys_errlist[tmperr]);
	write_user(user,text);
	write_syslog(text,0);
	return;
	}
write_user(user,"Unknown hostname.\n");
write_syslog("Unknown hostname.\n",0);
}



/*** Disconnect a link ***/
disconnect_netlink(user)
UR_OBJECT user;
{
RM_OBJECT rm;
NL_OBJECT nl;

if (word_count<2) {
	write_usage(user,"disconnect <room service is linked to>\n");  return;
	}
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
nl=rm->netlink;
if (nl==NULL) {
	write_user(user,"That room is not linked to a service.\n");
	return;
	}
if (nl->type==UNCONNECTED) {
	write_user(user,"That rooms netlink is not connected.\n");  return;
	}
/* If link has hung at verification stage don't bother announcing it */
if (nl->stage==UP) {
	sprintf(text,"[ ~OLSYSTEM:~RS Disconnecting from %s in the %s. ]\n",nl->service,rm->name);
	write_room(NULL,text);
	sprintf(text,"NETLINK: Link to %s in the %s disconnected by %s.\n",nl->service,rm->name,user->name);
	write_syslog(text,1);
	sprintf(text,"[ Link to %s in the %s disconnected by %s(%s). ]\n",nl->service,rm->name,user->name,levels[user->level]);
	write_level(ARCH,1,text,NULL);
	}
else {
	sprintf(text,"NETLINK: Link to %s disconnected by %s.\n",nl->service,user->name);
	write_syslog(text,1);
	sprintf(text,"[ Link to %s disconnected by %s(%s). ]\n",nl->service,user->name,levels[user->level]);
	write_level(ARCH,1,text,NULL);
	}
shutdown_netlink(nl);
write_user(user,"Disconnected.\n");
}


/*** Change users password. Only ARCHes and above can change another users 
	password and they do this by specifying the user at the end. When this is 
	done the old password given can be anything, the wiz doesnt have to know it
	in advance. ***/
change_pass(user)
UR_OBJECT user;
{
UR_OBJECT u;
char *name;

if (word_count<3) {
	if (user->level<SU)
		write_usage(user,"passwd <old password> <new password>\n");
	else write_usage(user,"passwd <old password> <new password> [<user>]\n");
	return;
	}
if (strlen(word[2])<3) {
	write_user(user,"New password too short.\n");  return;
	}
if (strlen(word[2])>PASS_LEN) {
	write_user(user,"New password too long.\n");  return;
	}
/* Change own password */
if (word_count==3) {
	if (strcmp((char *)crypt(word[1],"NU"),user->pass)) {
		write_user(user,"Old password incorrect.\n");  return;
		}
	if (!strcmp(word[1],word[2])) {
		write_user(user,"Old and new passwords are the same.\n");  return;
		}
	strcpy(user->pass,(char *)crypt(word[2],"NU"));
	save_user_details(user,0);
	cls(user);
	write_user(user,"Password changed.\n");
	return;
	}
/* Change someone elses */
if (user->level<SU) {
	write_user(user,"You are not a high enough level to use the user option.\n");  
	return;
	}
word[3][0]=toupper(word[3][0]);
if (!strcmp(word[3],user->name)) {
	/* security feature  - prevents someone coming to a wizes terminal and 
	   changing his password since he wont have to know the old one */
	write_user(user,"You cannot change your own password using the <user> option.\n");
	return;
	}
if (u=get_user(word[3])) {
	if (u->type==REMOTE_TYPE) {
		write_user(user,"You cannot change the password of a user logged on remotely.\n");
		return;
		}
	if (u->level>=user->level) {
		write_user(user,"You cannot change the password of a user of equal or higher level than yourself.\n");
		return;
		}
	strcpy(u->pass,(char *)crypt(word[2],"NU"));
	cls(user);
	sprintf(text,"%s's password has been changed.\n",u->name);
	write_user(user,text);
	if (user->vis) name=user->name; else name=invisname;
	sprintf(text,"~FR~OLYour password has been changed by %s!\n",name);
	write_user(u,text);
	sprintf(text,"%s changed %s's password.\n",user->name,u->name);
	write_syslog(text,1);
	sprintf(text,"[ %s(%s) changed %s's password. ]\n",user->name,levels[user->level],u->name);
	write_level(ARCH,1,text,NULL);
	return;
	}
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user object.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user object in change_pass().\n",0);
	return;
	}
strcpy(u->name,word[3]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);   
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->level>=user->level) {
	write_user(user,"You cannot change the password of a user of equal or higher level than yourself.\n");
	destruct_user(u);
	destructed=0;
	return;
	}
strcpy(u->pass,(char *)crypt(word[2],"NU"));
save_user_details(u,0);
cls(user);
sprintf(text,"%s's password changed to \"%s\".\n",word[3],word[2]);
write_user(user,text);
sprintf(text,"%s changed %s's password.\n",user->name,u->name);
write_syslog(text,1);
sprintf(text,"[ %s(%s) changed %s's password. ]\n",user->name,levels[user->level],u->name);
write_level(ARCH,1,text,NULL);
destruct_user(u);
destructed=0;
}


/*** Kill a user ***/
kill_user(user)
UR_OBJECT user;
{
UR_OBJECT victim;
RM_OBJECT rm;

if (word_count<2) {
	write_usage(user,"kill <user>\n");  return;
	}
if (!(victim=get_user(word[1]))) {
	write_user(user,notloggedon);  return;
	}
if (user==victim) {
	write_user(user,"Trying to commit suicide this way is the sixth sign of madness.\n");
	return;
	}
if (victim->level>=user->level) {
	write_user(user,"You cannot kill a user of equal or higher level than yourself.\n");
	sprintf(text,"%s tried to kill you!\n",user->name);
	write_user(victim,text);
	return;
	}
sprintf(text,"%s KILLED %s.\n",user->name,victim->name);
write_syslog(text,1);
sprintf(text,"[ %s(%s) has killed %s. ]\n",user->name,levels[user->level],victim->name);
write_level(ARCH,1,text,NULL);

if (user->vis) {
	sprintf(text,"~FM~OL%s scares the shit out of you.~RS\n",user->name);
	write_user(victim,text);
	}
else {
	sprintf(text,"~FM~OLSomeone scares the shit out of you.~RS\n");
	write_user(victim,text);
	}
disconnect_user(victim);
}


/*** Promote a user ***/
promote(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;
char text2[80];

if (word_count<2) {
	write_usage(user,"promote <user>\n");  return;
	}
/* See if user is on atm */
if ((u=get_user(word[1]))!=NULL) {
	if (u->level>=user->level) {
		write_user(user,"You cannot promote a user to a level higher than your own.\n");
		return;
		}
	u->level++;
	sprintf(text,"~FG~OLYou promote %s to level: ~RS~OL%s.\n",u->name,level_name[u->level]);
	write_user(user,text);
	rm=user->room;
	user->room=NULL;
	sprintf(text,"~FG~OL%s(%s) promotes %s to level: ~RS~OL%s.\n",user->name,levels[user->level],u->name,level_name[u->level]);
	write_room_except(NULL,text,u);
	user->room=rm;
	sprintf(text,"~FG~OL%s(%s) has promoted you to level: ~RS~OL%s!\n",user->name,levels[user->level],level_name[u->level]);
	write_user(u,text);
	sprintf(text,"%s(%s) PROMOTED %s to level %s.\n",user->name,levels[user->level],u->name,level_name[u->level]);
	write_syslog(text,1);
	sprintf(text,"[ %s(%s) has promoted %s to %s. ]\n",user->name,levels[user->level],u->name,level_name[u->level]);
	write_level(ARCH,1,text,NULL);
	return;
	}
/* Create a temp session, load details, alter , then save. This is inefficient
   but its simpler than the alternative */
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user object.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user object in promote().\n",0);
	return;
	}
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);  
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->level>=user->level) {
	write_user(user,"You cannot promote a user to a level higher than your own.\n");
	destruct_user(u);
	destructed=0;
	return;
	}
u->level++;  
u->socket=-2;
strcpy(u->site,u->last_site);
save_user_details(u,0);
sprintf(text,"You promote %s to level: ~OL%s.\n",u->name,level_name[u->level]);
write_user(user,text);
sprintf(text2,"~FG~OLYou have been promoted to level: ~RS~OL%s.\n",level_name[u->level]);
send_mail(user,word[1],text2);
sprintf(text,"%s(%s) PROMOTED %s to level %s.\n",user->name,levels[user->level],word[1],level_name[u->level]);
write_syslog(text,1);
sprintf(text,"[ %s(%s) has promoted %s to %s. ]\n",user->name,levels[user->level],u->name,level_name[u->level]);
write_level(ARCH,1,text,NULL);
destruct_user(u);
destructed=0;
}


/*** Demote a user ***/
demote(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;
char text2[80],*name;

if (word_count<2) {
	write_usage(user,"demote <user>\n");  return;
	}
/* See if user is on atm */
if ((u=get_user(word[1]))!=NULL) {
	if (u->level==DUNCE) {
		write_user(user,"You cannot demote a user of level DUNCE.\n");
		return;
		}
	if (u->level>=user->level) {
		write_user(user,"You cannot demote a user of an equal or higher level than yourself.\n");
		return;
		}
	if (user->vis) name=user->name; else name=invisname;
	u->level--;
	sprintf(text,"~FR~OLYou demote %s to level: ~RS~OL%s.\n",u->name,level_name[u->level]);
	write_user(user,text);
	rm=user->room;
	user->room=NULL;
	sprintf(text,"~FR~OL%s(%s) demotes %s to level: ~RS~OL%s.\n",name,levels[user->level],u->name,level_name[u->level]);
	write_room_except(NULL,text,u);
	user->room=rm;
	sprintf(text,"~FR~OL%s(%s) has demoted you to level: ~RS~OL%s!\n",name,levels[user->level],level_name[u->level]);
	write_user(u,text);
	sprintf(text,"%s(%s) DEMOTED %s to level %s.\n",user->name,levels[user->level],u->name,level_name[u->level]);
	write_syslog(text,1);
	sprintf(text,"[ %s(%s) has demoted %s to %s. ]\n",user->name,levels[user->level],u->name,level_name[u->level]);
	write_level(ARCH,1,text,NULL);
	return;
	}
/* User not logged on */
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user object.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user object in demote().\n",0);
	return;
	}
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);  
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->level==DUNCE) {
	write_user(user,"You cannot demote a user of level DUNCE.\n");
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->level>=user->level) {
	write_user(user,"You cannot demote a user of an equal or higher level than yourself.\n");
	destruct_user(u);
	destructed=0;
	return;
	}
u->level--;
u->socket=-2;
strcpy(u->site,u->last_site);
save_user_details(u,0);
sprintf(text,"You demote %s to level: ~OL%s.\n",u->name,level_name[u->level]);
write_user(user,text);
sprintf(text2,"~FR~OLYou have been demoted to level: ~RS~OL%s.\n",level_name[u->level]);
send_mail(user,word[1],text2);
sprintf(text,"%s(%s) DEMOTED %s to level %s.\n",user->name,levels[user->level],word[1],level_name[u->level]);
write_syslog(text,1);
sprintf(text,"[ %s(%s) has demoted %s to %s. ]\n",user->name,levels[user->level],u->name,level_name[u->level]);
write_level(ARCH,1,text,NULL);
destruct_user(u);
destructed=0;
}


/*** List banned sites or users ***/
listbans(user)
UR_OBJECT user;
{
int i;
char filename[80];

if (!strcmp(word[1],"sites")) {
	write_user(user,"\n~BB*** Banned sites/domains ***\n\n"); 
	sprintf(filename,"%s/%s",DATAFILES,SITEBAN);
	switch(more(user,user->socket,filename)) {
		case 0:
		write_user(user,"There are no banned sites/domains.\n\n");
		return;

		case 1: user->misc_op=2;
		}
	return;
	}
if (!strcmp(word[1],"users")) {
	write_user(user,"\n~BBBanned users:\n\n");
	sprintf(filename,"%s/%s",DATAFILES,USERBAN);
	switch(more(user,user->socket,filename)) {
		case 0:
		write_user(user,"There are no banned users.\n\n");
		return;

		case 1: user->misc_op=2;
		}
	return;
	}
if (!strcmp(word[1],"swears")) {
	write_user(user,"\n~BBBanned swear words:\n\n");
	i=0;
	while(swear_words[i][0]!='*') {
		write_user(user,swear_words[i]);
		write_user(user,"\n");
		++i;
		}
	if (!i) write_user(user,"There are no banned swear words.\n");
	if (ban_swearing) write_user(user,"\n");
	else write_user(user,"\n(Swearing ban is currently off)\n\n");
	return;
	}
write_usage(user,"listbans sites/users/swears\n"); 
}

/*** Ban a site/domain or user ***/
ban(user)
UR_OBJECT user;
{
/* char *usage="Usage: ban site/user <site/user name>\n"; */

if (word_count<3) {
     write_usage(user,".ban site <site to be banned>\n");
     write_user(user,"   i.e. .ban site .theirsite.com\n");
     write_user(user,"   i.e. .ban site 198.123.241.  ~OL(omit last number)\n\n");
     write_usage(user,".ban user <username>\n\n");
     return;
     }
if (!strcmp(word[1],"site")) {  ban_site(user);  return;  }
if (!strcmp(word[1],"user")) {  ban_user(user);  return;  }

/* Neither site/user was specified, show usage! */
write_usage(user,".ban site <site to be banned>\n");
write_user(user,"   i.e. .ban site .theirsite.com\n");
write_user(user,"   i.e. .ban site 198.123.241.     ~OL(omit last number)\n\n");
write_usage(user,".ban user <username>\n\n");
}

ban_site(user)
UR_OBJECT user;
{
FILE *fp;
char filename[80],host[81],site[80];

gethostname(host,80);
if (!strcasecmp(word[2],host) || (!strcasecmp(word[2],"localhost")) || (!strncmp(word[2],"127.",4))) {
	write_user(user,"You cannot ban the machine that this program is running on.\n");
	return;
	}
sprintf(filename,"%s/%s",DATAFILES,SITEBAN);

/* See if ban already set for given site */
if (fp=fopen(filename,"r")) {
	fscanf(fp,"%s",site);
	while(!feof(fp)) {
		if (!strcmp(site,word[2])) {
			write_user(user,"That site/domain is already banned.\n");
			fclose(fp);  return;
			}
		fscanf(fp,"%s",site);
		}
	fclose(fp);
	}

/* Write new ban to file */
if (!(fp=fopen(filename,"a"))) {
	sprintf(text,"%s: Can't open file to append.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Couldn't open file to append in ban_site().\n",0);
	return;
	}
fprintf(fp,"%s\n",word[2]);
fclose(fp);
write_user(user,"Site/domain banned.\n");
sprintf(text,"%s BANNED site/domain %s.\n",user->name,word[2]);
write_syslog(text,1);
sprintf(text,"[ %s(%s) has banned site/domain %s. ]\n",user->name,levels[user->level],word[2]);
write_level(ARCH,1,text,NULL);
}


ban_user(user)
UR_OBJECT user;
{
UR_OBJECT u;
FILE *fp;
char filename[80],filename2[80],p[20],name[USER_NAME_LEN+1];
int a,b,c,d,level;

word[2][0]=toupper(word[2][0]);
if (!strcmp(user->name,word[2])) {
	write_user(user,"Trying to ban yourself is the seventh sign of madness.\n");
	return;
        }
/* See if ban already set for given user */
sprintf(filename,"%s/%s",DATAFILES,USERBAN);
if (fp=fopen(filename,"r")) {
	fscanf(fp,"%s",name);
	while(!feof(fp)) {
		if (!strcmp(name,word[2])) {
			write_user(user,"That user is already banned.\n");
			fclose(fp);  return;
			}
		fscanf(fp,"%s",name);
		}
	fclose(fp);
	}

/* See if already on */
if ((u=get_user(word[2]))!=NULL) {
	if (u->level>=user->level) {
		write_user(user,"You cannot ban a user of equal or higher level than yourself.\n");
		return;
		}
	}
else {
	/* User not on so load up his data */
	sprintf(filename2,"%s/%s.D",USERFILES,word[2]);
	if (!(fp=fopen(filename2,"r"))) {
		write_user(user,nosuchuser);  return;
		}
	fscanf(fp,"%s\n%d %d %d %d %d",p,&a,&b,&c,&d,&level);
	fclose(fp);
	if (level>=user->level) {
		write_user(user,"You cannot ban a user of equal or higher level than yourself.\n");
		return;
		}
	}

/* Write new ban to file */
if (!(fp=fopen(filename,"a"))) {
	sprintf(text,"%s: Can't open file to append.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Couldn't open file to append in ban_user().\n",0);
	return;
	}
fprintf(fp,"%s\n",word[2]);
fclose(fp);
write_user(user,"User banned.\n");
sprintf(text,"%s BANNED user %s.\n",user->name,word[2]);
write_syslog(text,1);
sprintf(text,"[ %s(%s) has banned user %s. ]\n",user->name,levels[user->level],word[2]);
write_level(ARCH,1,text,NULL);
if (u!=NULL) {
	write_user(u,"\n\07~FR~OL~LIYou have been banned from here!\n\n");
	disconnect_user(u);
	}
}

	

/*** uban a site (or domain) or user ***/
unban(user)
UR_OBJECT user;
{
char *usage="Usage: unban site/user <site/user name>\n";

if (word_count<3) {
	write_user(user,usage);  return;
	}
if (!strcmp(word[1],"site")) {  unban_site(user);  return;  }
if (!strcmp(word[1],"user")) {  unban_user(user);  return;  }
write_user(user,usage);
}


unban_site(user)
UR_OBJECT user;
{
FILE *infp,*outfp;
char filename[80],site[80];
int found,cnt;

sprintf(filename,"%s/%s",DATAFILES,SITEBAN);
if (!(infp=fopen(filename,"r"))) {
	write_user(user,"That site/domain is not currently banned.\n");
	return;
	}
if (!(outfp=fopen("tempfile","w"))) {
	sprintf(text,"%s: Couldn't open tempfile.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Couldn't open tempfile to write in unban_site().\n",0);
	fclose(infp);
	return;
	}
found=0;   cnt=0;
fscanf(infp,"%s",site);
while(!feof(infp)) {
	if (strcmp(word[2],site)) {  
		fprintf(outfp,"%s\n",site);  cnt++;  
		}
	else found=1;
	fscanf(infp,"%s",site);
	}
fclose(infp);
fclose(outfp);
if (!found) {
	write_user(user,"That site/domain is not currently banned.\n");
	unlink("tempfile");
	return;
	}
if (!cnt) {
	unlink(filename);  unlink("tempfile");
	}
else rename("tempfile",filename);
write_user(user,"Site ban removed.\n");
sprintf(text,"%s UNBANNED site %s.\n",user->name,word[2]);
write_syslog(text,1);
sprintf(text,"[ %s(%s) has unbanned site %s. ]\n",user->name,levels[user->level],word[2]);
write_level(ARCH,1,text,NULL);
}


unban_user(user)
UR_OBJECT user;
{
FILE *infp,*outfp;
char filename[80],name[USER_NAME_LEN+1];
int found,cnt;

sprintf(filename,"%s/%s",DATAFILES,USERBAN);
if (!(infp=fopen(filename,"r"))) {
	write_user(user,"That user is not currently banned.\n");
	return;
	}
if (!(outfp=fopen("tempfile","w"))) {
	sprintf(text,"%s: Couldn't open tempfile.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Couldn't open tempfile to write in unban_user().\n",0);
	fclose(infp);
	return;
	}
found=0;  cnt=0;
word[2][0]=toupper(word[2][0]);
fscanf(infp,"%s",name);
while(!feof(infp)) {
	if (strcmp(word[2],name)) {
		fprintf(outfp,"%s\n",name);  cnt++;
		}
	else found=1;
	fscanf(infp,"%s",name);
	}
fclose(infp);
fclose(outfp);
if (!found) {
	write_user(user,"That user is not currently banned.\n");
	unlink("tempfile");
	return;
	}
if (!cnt) {
	unlink(filename);  unlink("tempfile");
	}
else rename("tempfile",filename);
write_user(user,"User ban removed.\n");
sprintf(text,"%s UNBANNED user %s.\n",user->name,word[2]);
write_syslog(text,1);
sprintf(text,"[ %s(%s) has unbanned user %s. ]\n",user->name,levels[user->level],word[2]);
write_level(ARCH,1,text,NULL);
}



/*** Set user visible or invisible ***/
visibility(user,vis)
UR_OBJECT user;
int vis;
{
if (vis) {
	if (user->vis) {
		write_user(user,"You are already visible.\n");  return;
		}
	write_user(user,"~FB~OLYou appear.\n");
	sprintf(text,"~FB~OL%s appears.\n",user->name);
	write_room_except(user->room,text,user);
	user->vis=1;
	num_of_users++;
	return;
	}
if (!user->vis) {
	write_user(user,"You are already invisible.\n");  return;
	}
write_user(user,"~FB~OLYou disappear.\n");
sprintf(text,"~FB~OL%s disappears.\n",user->name);
write_room_except(user->room,text,user);
num_of_users--;
user->vis=0;
}


/*** Site an user ***/
site(user)
UR_OBJECT user;
{
UR_OBJECT u;

if (word_count<2) {
	write_usage(user,"site <user>");  return;
	}
/* User currently logged in */
if (u=get_user(word[1])) {
	if(u->type==REMOTE_TYPE)
	  sprintf(text,"%s is remotely connected from ~FY~OL%s~RS",u->name,u->site);
	else
	sprintf(text,"%s is logged in from ~FY~OL%s~RS (%s port)",u->name,u->site,u->port==port[0]?"Main":"Wiz");
	write_user(user,text);
	sprintf(text," - idle for ~FT%d~RS seconds.\n",(int)(time(0) - u->last_input));
	write_user(user,text);
	return;
	}
/* User not logged in */
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user object.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user object in site().\n",0);
	return;
	}
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);
	destruct_user(u);
	destructed=0;
	return;
	}
sprintf(text,"%s was last logged in from ~FY~OL%s.\n",word[1],u->last_site);
write_user(user,text);
destruct_user(u);
destructed=0;
}

/*** Wake up some sleepy herbert ***/
wake(user)
UR_OBJECT user;
{
UR_OBJECT u;
char *name;

if (word_count<2) {
	write_usage(user,"wake <user>\n");  return;
	}
if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot wake anyone.\n");  return;
	}
if (!(u=get_user(word[1]))) {
	write_user(user,notloggedon);  return;
	}
if (u==user) {
	write_user(user,"Trying to wake yourself up is the eighth sign of madness.\n");
	return;
	}
if (u->afk) {
	write_user(user,"You cannot wake someone who is AFK.\n");  return;
	}
if (u->vis) {
	if (user->vis) name=user->name; else name=invisname;
	sprintf(text,"\07\n~BR*** %s says: ~OL~LIWake up!!!~RS~BR ***\n\n",name);
	write_user(u,text);
	write_user(user,"Wake up call sent.\n");
	}
else write_usage(user,"wake <user>\n");
}


/*** WiZ Channel ***/
wiz(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char *name;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot chat on the wiz channel.\n");  return;
	}
if (!strcmp(word[1],"on")) {                    
	if (user->ignwiz) {
		write_user(user,"Wiz channel on.\n");
		user->ignwiz=0;
                if (user->vis) name=user->name; else name=invisname;
                sprintf(text,"~OL[Wiz]~RS %s(%s) turns the wiz channel on.\n",name,levels[user->level]);
                write_room(NULL,text);
		return;
		}
	else {
		write_user(user,"You already have the Wiz channel on.\n");
        	return;                                 
	        }
        }
if (!strcmp(word[1],"off")) {
	if (user->ignwiz) {
		write_user(user,"You already have the Wiz channel off.\n");
		return;
		}
	else {
		write_user(user,"Wiz channel off.\n");
		user->ignwiz=1;                       
                if (user->vis) name=user->name; else name=invisname;
                sprintf(text,"~OL[Wiz]~RS %s(%s) turns the wiz channel off.\n",name,levels[user->level]);
                write_room(NULL,text);
		return;
		}
        }                                       
if (user->ignwiz) {
	write_user(user,"You currently have the wiz channel off.\n");  return;
	}
if (word_count<2) {
	write_user(user,"Wiz what?\n");  return;
	}
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"~OL[Wiz]~RS %s: %s\n",name,inpstr);
write_room(NULL,text);
}

/*** Muzzle an annoying user so he cant speak, emote, echo, write, smail
     or bcast. Muzzles have levels from WIZARD to SU so for instance a wiz
     cannot remove a muzzle set by a god.  ***/
muzzle(user)
UR_OBJECT user;
{
UR_OBJECT u;

if (word_count<2) {
	write_usage(user,"muzzle <user>\n");  return;
	}
if ((u=get_user(word[1]))!=NULL) {
	if (u==user) {
		write_user(user,"Trying to muzzle yourself is the ninth sign of madness.\n");
		return;
		}
	if (u->level>=user->level) {
		write_user(user,"You cannot muzzle a user of equal or higher level than yourself.\n");
		return;
		}
	if (u->muzzled>=user->level) {
		sprintf(text,"%s is already muzzled.\n",u->name);
		write_user(user,text);  return;
		}
	sprintf(text,"~FR~OL%s now has a muzzle of level: ~RS~OL%s.\n",u->name,level_name[user->level]);
	write_user(user,text);
	write_user(u,"~FR~OLYou have been muzzled!\n");
	sprintf(text,"%s muzzled %s.\n",user->name,u->name);
	write_syslog(text,1);
	sprintf(text,"[ %s(%s) has muzzled user %s. ]\n",user->name,levels[user->level],u->name);
	write_level(ARCH,1,text,NULL);

	u->muzzled=user->level;
	return;
	}
/* User not logged on */
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user object.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user object in muzzle().\n",0);
	return;
	}
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);  
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->level>=user->level) {
	write_user(user,"You cannot muzzle a user of equal or higher level than yourself.\n");
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->muzzled>=user->level) {
	sprintf(text,"%s is already muzzled.\n",u->name);
	write_user(user,text); 
	destruct_user(u);
	destructed=0;
	return;
	}
u->socket=-2;
u->muzzled=user->level;
strcpy(u->site,u->last_site);
save_user_details(u,0);
sprintf(text,"~FR~OL%s given a muzzle of level: ~RS~OL%s.\n",u->name,level_name[user->level]);
write_user(user,text);
send_mail(user,word[1],"~FR~OLYou have been muzzled!\n");
sprintf(text,"%s muzzled %s.\n",user->name,u->name);
write_syslog(text,1);
sprintf(text,"[ %s(%s) has muzzled user %s. ]\n",user->name,levels[user->level],u->name);
write_level(ARCH,1,text,NULL);
destruct_user(u);
destructed=0;
}



/*** Umuzzle the bastard now he's apologised and grovelled enough via email ***/
unmuzzle(user)
UR_OBJECT user;
{
UR_OBJECT u;

if (word_count<2) {
	write_usage(user,"unmuzzle <user>\n");  return;
	}
if ((u=get_user(word[1]))!=NULL) {
	if (u==user) {
		write_user(user,"Trying to unmuzzle yourself is the tenth sign of madness.\n");
		return;
		}
	if (!u->muzzled) {
		sprintf(text,"%s is not muzzled.\n",u->name);  return;
		}
	if (u->muzzled>user->level) {
		sprintf(text,"%s's muzzle is set to level %s, you do not have the power to remove it.\n",u->name,level_name[u->muzzled]);
		write_user(user,text);  return;
		}
	sprintf(text,"~FG~OLYou remove %s's muzzle.\n",u->name);
	write_user(user,text);
	write_user(u,"~FG~OLYou have been unmuzzled!\n");
	sprintf(text,"%s unmuzzled %s.\n",user->name,u->name);
	write_syslog(text,1);
	sprintf(text,"[ %s(%s) has unmuzzled user %s. ]\n",user->name,levels[user->level],u->name);
	write_level(ARCH,1,text,NULL);
	u->muzzled=0;
	return;
	}
/* User not logged on */
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user object.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user object in unmuzzle().\n",0);
	return;
	}
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);  
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->muzzled>user->level) {
	sprintf(text,"%s's muzzle is set to level %s, you do not have the power to remove it.\n",u->name,level_name[u->muzzled]);
	write_user(user,text);  
	destruct_user(u);
	destructed=0;
	return;
	}
u->socket=-2;
u->muzzled=0;
strcpy(u->site,u->last_site);
save_user_details(u,0);
sprintf(text,"~FG~OLYou remove %s's muzzle.\n",u->name);
write_user(user,text);
send_mail(user,word[1],"~FG~OLYou have been unmuzzled.\n");
sprintf(text,"%s unmuzzled %s.\n",user->name,u->name);
write_syslog(text,1);
sprintf(text,"[ %s(%s) has unmuzzled user %s. ]\n",user->name,levels[user->level],u->name);
write_level(ARCH,1,text,NULL);
destruct_user(u);
destructed=0;
}



/*** Switch system logging on and off ***/
logging(user)
UR_OBJECT user;
{
if (system_logging) {
	write_user(user,"System logging ~FRoff.\n");
	sprintf(text,"%s switched system logging off.\n",user->name);
	write_syslog(text,1);
	sprintf(text,"[ %s(%s) has switched system logging off. ]\n",user->name,levels[user->level]);
	write_level(ARCH,1,text,NULL);
	system_logging=0;
	return;
	}
system_logging=1;
write_user(user,"System logging ~FGon.\n");
sprintf(text,"%s switched system logging on.\n",user->name);
write_syslog(text,1);
sprintf(text,"[ %s(%s) has switched system logging on. ]\n",user->name,levels[user->level]);
write_level(ARCH,1,text,NULL);
}

/*** Show talker system parameters etc ***/
system_details(user)
UR_OBJECT user;
{
NL_OBJECT nl;
RM_OBJECT rm;
UR_OBJECT u;
char bstr[40],minlogin[5];
char *ca[]={ "NONE  ","IGNORE","REBOOT" };
int days,hours,mins,secs;
int netlinks,live,inc,outg;
int rms,inlinks,num_clones,mem,size;

sprintf(text,"\n~BB*** Bolts version %s - System status ***\n\n",boltsversion);
write_user(user,text);

/* Get some values */
strcpy(bstr,ctime(&boot_time));
secs=(int)(time(0)-boot_time);
days=secs/86400;
hours=(secs%86400)/3600;
mins=(secs%3600)/60;
secs=secs%60;
num_clones=0;
mem=0;
size=sizeof(struct user_struct);
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE) num_clones++;
	mem+=size;
	}

rms=0;  
inlinks=0;
size=sizeof(struct room_struct);
for(rm=room_first;rm!=NULL;rm=rm->next) {
	if (rm->inlink) ++inlinks;
	++rms;  mem+=size;
	}

netlinks=0;  
live=0;
inc=0; 
outg=0;
size=sizeof(struct netlink_struct);
for(nl=nl_first;nl!=NULL;nl=nl->next) {
	if (nl->type!=UNCONNECTED && nl->stage==UP) live++;
	if (nl->type==INCOMING) ++inc;
	if (nl->type==OUTGOING) ++outg;
	++netlinks;  mem+=size;
	}
if (minlogin_level==-1) strcpy(minlogin,"NONE");
else strcpy(minlogin,level_name[minlogin_level]);

/* Show header parameters */
sprintf(text,"~FTProcess ID   : ~FG%d\n~FTTalker booted: ~FG%s~FTUptime       : ~FG%d days, %d hours, %d minutes, %d seconds\n",getpid(),bstr,days,hours,mins,secs);
write_user(user,text);
sprintf(text,"~FTPorts (M/W/L): ~FG%d,  %d,  %d\n\n",port[0],port[1],port[2]);
write_user(user,text);

/* Show others */
sprintf(text,"Max users              : %-3d          Current num. of users  : %d\n",max_users,num_of_users);
write_user(user,text);
sprintf(text,"Max clones             : %-2d           Current num. of clones : %d\n",max_clones,num_clones);
write_user(user,text);
sprintf(text,"Current minlogin level : %-4s         Login idle time out    : %d secs.\n",minlogin,login_idle_time);
write_user(user,text);
sprintf(text,"User idle time out     : %-4d secs.   Heartbeat              : %d\n",user_idle_time,heartbeat);
write_user(user,text);
sprintf(text,"Remote user maxlevel   : %-4s         Remote user deflevel   : %s\n",level_name[rem_user_maxlevel],level_name[rem_user_deflevel]);
write_user(user,text);
sprintf(text,"Wizport min login level: %-4s         Gatecrash level        : %s\n",level_name[wizport_level],level_name[gatecrash_level]);
write_user(user,text);
sprintf(text,"Time out maxlevel      : %-4s         Private room min count : %d\n",level_name[time_out_maxlevel],min_private_users);
write_user(user,text);
sprintf(text,"Message lifetime       : %-2d days      Message check time     : %02d:%02d\n",mesg_life,mesg_check_hour,mesg_check_min);
write_user(user,text);
sprintf(text,"Net idle time out      : %-4d secs.   Number of rooms        : %d\n",net_idle_time,rms);
write_user(user,text);
sprintf(text,"Num. accepting connects: %-2d           Total netlinks         : %d\n",inlinks,netlinks);
write_user(user,text);
sprintf(text,"Number which are live  : %-2d           Number incoming        : %d\n",live,inc);
write_user(user,text);
sprintf(text,"Number outgoing        : %-2d           Ignoring sigterm       : %s\n",outg,noyes2[ignore_sigterm]);
write_user(user,text);
sprintf(text,"Echoing passwords      : %s          Swearing banned        : %s\n",noyes2[password_echo],noyes2[ban_swearing]);
write_user(user,text);
sprintf(text,"Time out afks          : %s          Allowing caps in name  : %s\n",noyes2[time_out_afks],noyes2[allow_caps_in_name]);
write_user(user,text);
sprintf(text,"New user prompt default: %s          New user colour default: %s\n",offon[prompt_def],offon[colour_def]);
write_user(user,text);
sprintf(text,"New user charecho def. : %s          System logging         : %s\n",offon[charecho_def],offon[system_logging]);
write_user(user,text);
sprintf(text,"Crash action           : %s       Object memory allocated: %d\n\n",ca[crash_action],mem);
write_user(user,text);
sprintf(text,"%s was compiled at %s on %s by %s.\n",talkername,COMPILE_TIME,COMPILE_DATE,COMPILE_BY);
write_user(user,text);
sprintf(text,"Running on a %s with %s %s.\n\n",COMPILE_HOSTTYPE,COMPILE_OSNAME,COMPILE_RELEASE);
write_user(user,text);
}


/*** Set the character mode echo on or off. This is only for users logging in
     via a character mode client, those using a line mode client (eg unix
     telnet) will see no effect. ***/
toggle_charecho(user)
UR_OBJECT user;
{
if (!user->charmode_echo) {
	write_user(user,"Echoing for character mode clients ~FGon.\n");
	user->charmode_echo=1;
	}
else {
	write_user(user,"Echoing for character mode clients ~FRoff.\n");
	user->charmode_echo=0;
	}
if (user->room==NULL) prompt(user);
}


/*** Free a hung socket ***/
clearline(user)
UR_OBJECT user;
{
UR_OBJECT u;
int sock;

if (word_count<2 || !isnumber(word[1])) {
	write_usage(user,"clearline <line>\n");  return;
	}
sock=atoi(word[1]);

/* Find line amongst users */
for(u=user_first;u!=NULL;u=u->next) 
	if (u->type!=CLONE_TYPE && u->socket==sock) goto FOUND;
write_user(user,"That line is not currently active.\n");
return;

FOUND:
if (!u->login) {
	write_user(user,"You cannot clear the line of a logged in user.\n");
	return;
	}
write_user(u,"\n\nThis line is being cleared.\n\n");
disconnect_user(u); 
sprintf(text,"%s cleared line %d.\n",user->name,sock);
write_syslog(text,1);
sprintf(text,"[ %s(%s) has cleared line %d. ]\n",user->name,levels[user->level],sock);
write_level(ARCH,1,text,NULL);
sprintf(text,"Line %d cleared.\n",sock);
write_user(user,text);
destructed=0;
no_prompt=0;
}


/*** Change whether a rooms access is fixed or not ***/
change_room_fix(user,fix)
UR_OBJECT user;
int fix;
{
RM_OBJECT rm;
char *name;

if (word_count<2) rm=user->room;
else {
	if ((rm=get_room(word[1]))==NULL) {
		write_user(user,nosuchroom);  return;
		}
	}
if (user->vis) name=user->name; else name=invisname;
if (fix) {	
	if (rm->access & FIXED) {
		if (rm==user->room) 
			write_user(user,"This room's access is already fixed.\n");
		else write_user(user,"That room's access is already fixed.\n");
		return;
		}
	sprintf(text,"Access for room %s is now ~FRfixed.\n",rm->name);
	write_user(user,text);
	if (user->room==rm) {
		sprintf(text,"%s has ~FRfixed~RS access for this room.\n",name);
		write_room_except(rm,text,user);
		}
	else {
		sprintf(text,"This room's access has been ~FRfixed.\n");
		write_room(rm,text);
		}
	sprintf(text,"%s FIXED access to room %s.\n",user->name,rm->name);
	write_syslog(text,1);
	sprintf(text,"[ %s(%s) FIXED access to room %s. ]\n",user->name,levels[user->level],rm->name);
	write_level(ARCH,1,text,NULL);
	rm->access+=2;
	return;
	}
if (!(rm->access & FIXED)) {
	if (rm==user->room) 
		write_user(user,"This room's access is already unfixed.\n");
	else write_user(user,"That room's access is already unfixed.\n");
	return;
	}
sprintf(text,"Access for room %s is now ~FGunfixed.\n",rm->name);
write_user(user,text);
if (user->room==rm) {
	sprintf(text,"%s has ~FGunfixed~RS access for this room.\n",name);
	write_room_except(rm,text,user);
	}
else {
	sprintf(text,"This room's access has been ~FGunfixed.\n");
	write_room(rm,text);
	}
sprintf(text,"%s UNFIXED access to room %s.\n",user->name,rm->name);
write_syslog(text,1);
sprintf(text,"[ %s(%s) UNFIXED access to room %s. ]\n",user->name,levels[user->level],rm->name);
write_level(ARCH,1,text,NULL);
rm->access-=2;
reset_access(rm);
}



/*** View the system log ***/
viewlog(user)
UR_OBJECT user;
{
FILE *fp;
char c,*emp="\nThe system log is empty.\n";
int lines,cnt,cnt2;

if (word_count==1) {
	write_user(user,"\n~BBSystem log:\n\n");
	switch(more(user,user->socket,SYSLOG)) {
		case 0: write_user(user,emp);  return;
		case 1: user->misc_op=2; 
		}
	return;
	}
if ((lines=atoi(word[1]))<1) {
	write_usage(user,"viewlog [<lines from the end>]\n");  return;
	}
/* Count total lines */
if (!(fp=fopen(SYSLOG,"r"))) {  write_user(user,emp);  return;  }
cnt=0;

c=getc(fp);
while(!feof(fp)) {
	if (c=='\n') ++cnt;
	c=getc(fp);
	}
if (cnt<lines) {
	sprintf(text,"There are only %d lines in the log.\n",cnt);
	write_user(user,text);
	fclose(fp);
	return;
	}
if (cnt==lines) {
	write_user(user,"\n~BBSystem log:\n\n");
	fclose(fp);  more(user,user->socket,SYSLOG);  return;
	}

/* Find line to start on */
fseek(fp,0,0);
cnt2=0;
c=getc(fp);
while(!feof(fp)) {
	if (c=='\n') ++cnt2;
	c=getc(fp);
	if (cnt2==cnt-lines) {
		sprintf(text,"\n~BBSystem log (last %d lines):\n\n",lines);
		write_user(user,text);
		user->filepos=ftell(fp)-1;
		fclose(fp);
		if (more(user,user->socket,SYSLOG)!=1) user->filepos=0;
		else user->misc_op=2;
		return;
		}
	}
fclose(fp);
sprintf(text,"%s: Line count error.\n",syserror);
write_user(user,text);
write_syslog("ERROR: Line count error in viewlog().\n",0);
}


/*** Clear the review buffer ***/
revclr(user)
UR_OBJECT user;
{
char *name;

clear_revbuff(user->room); 
write_user(user,"Review buffer cleared.\n");
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"%s has cleared the review buffer.\n",name);
write_room_except(user->room,text,user);
}


/*** Clone a user in another room ***/
create_clone(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;
char *name;
int cnt;

/* Check room */
if (word_count<2) rm=user->room;
else {
	if ((rm=get_room(word[1]))==NULL) {
		write_user(user,nosuchroom);  return;
		}
	}	
/* If room is private then nocando */
if (!has_room_access(user,rm)) {
	write_user(user,"That room is currently private, you cannot create a clone there.\n");  
	return;
	}
/* Count clones and see if user already has a copy there , no point having 
   2 in the same room */
cnt=0;
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE && u->owner==user) {
		if (u->room==rm) {
			sprintf(text,"You already have a clone in the %s.\n",rm->name);
			write_user(user,text);
			return;
			}	
		if (++cnt==max_clones) {
			write_user(user,"You already have the maximum number of clones allowed.\n");
			return;
			}
		}
	}
/* Create clone */
if ((u=create_user())==NULL) {		
	sprintf(text,"%s: Unable to create copy.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create user copy in clone().\n",0);
	return;
	}
u->type=CLONE_TYPE;
u->socket=user->socket;
u->room=rm;
u->owner=user;
if (user->vis) name=user->name; else name=invisname;
strcpy(u->name,name);
strcpy(u->desc,"~BR(CLONE)");

if (rm==user->room)
	write_user(user,"~FB~OLYou create a clone here.\n");
else {
	sprintf(text,"~FB~OLYou create a clone in the %s room.\n",rm->name);
	write_user(user,text);
	}
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"~FB~OL%s mumbles something...\n",name);
write_room_except(user->room,text,user);
sprintf(text,"~FB~OLA clone of %s appears out of the ground.\n",name);
write_room_except(rm,text,user);
}


/*** Destroy user clone ***/
destroy_clone(user)
UR_OBJECT user;
{
UR_OBJECT u,u2;
RM_OBJECT rm;
char *name;

/* Check room and user */
if (word_count<2) rm=user->room;
else {
	if ((rm=get_room(word[1]))==NULL) {
		write_user(user,nosuchroom);  return;
		}
	}
if (word_count>2) {
	if ((u2=get_user(word[2]))==NULL) {
		write_user(user,notloggedon);  return;
		}
	if (u2->level>=user->level) {
		write_user(user,"You cannot destroy the clone of a user of an equal or higher level.\n");
		return;
		}
	}
else u2=user;
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE && u->room==rm && u->owner==u2) {
		destruct_user(u);
		reset_access(rm);
		write_user(user,"~FM~OLYou throw a bomb at the clone as it bursts into flames.\n");
		if (user->vis) name=user->name; else name=invisname;
		sprintf(text,"~FM~OL%s mumbles something...\n",name);
		write_room_except(user->room,text,user);
		if (u2->vis) sprintf(text,"~FM~OLThe clone of %s is hit by a bomb and bursts into flames!\n",u2->name);
		else sprintf(text,"~FM~OLThe clone of Someone is hit by a bomb and bursts into flames!\n");
		write_room(rm,text);
		if (u2!=user) {
			sprintf(text,"[ ~OLSYSTEM: ~FR%s has destroyed your clone in the %s. ]\n",user->name,rm->name);
			write_user(u2,text);
			}
		destructed=0;
		return;
		}
	}
if (u2==user) sprintf(text,"You do not have a clone in the %s.\n",rm->name);
else sprintf(text,"%s does not have a clone the %s.\n",u2->name,rm->name);
write_user(user,text);
}


/*** Show users own clones ***/
myclones(user)
UR_OBJECT user;
{
UR_OBJECT u;
int cnt;

cnt=0;
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type!=CLONE_TYPE || u->owner!=user) continue;
	if (++cnt==1) 
		write_user(user,"\n~BBRooms you have clones in:\n\n");
	sprintf(text,"  %s\n",u->room);
	write_user(user,text);
	}
if (!cnt) write_user(user,"You have no clones.\n");
else {
	sprintf(text,"\nTotal of %d clones.\n\n",cnt);
	write_user(user,text);
	}
}


/*** Show all clones on the system ***/
allclones(user)
UR_OBJECT user;
{
UR_OBJECT u;
int cnt;

cnt=0;
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type!=CLONE_TYPE) continue;
	if (++cnt==1) {
		sprintf(text,"\n~BBCurrent clones %s:\n\n",long_date(1));
		write_user(user,text);
		}
	sprintf(text,"%-15s : %s\n",u->name,u->room);
	write_user(user,text);
	}
if (!cnt) write_user(user,"There are no clones on the system.\n");
else {
	sprintf(text,"\nTotal of %d clones.\n\n",cnt);
	write_user(user,text);
	}
}


/*** User swaps places with his own clone. All we do is swap the rooms the
	objects are in. ***/
clone_switch(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;

if (word_count<2) {
	write_usage(user,"switch <room clone is in>\n");  return;
	}
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE && u->room==rm && u->owner==user) {
		write_user(user,"\n~FB~OLYou experience a strange sensation...\n");
		u->room=user->room;
		user->room=rm;
		sprintf(text,"The clone of %s comes alive!\n",u->name);
		write_room_except(user->room,text,user);
		sprintf(text,"%s turns into a clone!\n",u->name);
		write_room_except(u->room,text,u);
		look(user);
		return;
		}
	}
write_user(user,"You do not have a clone in that room.\n");
}


/*** Make a clone speak ***/
clone_say(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
RM_OBJECT rm;
UR_OBJECT u;

if (user->muzzled) {
	write_user(user,"You are muzzled, your clone cannot speak.\n");
	return;
	}
if (word_count<3) {
	write_usage(user,"csay <room clone is in> <message>\n");
	return;
	}
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE && u->room==rm && u->owner==user) {
		say(u,remove_first(inpstr));  return;
		}
	}
write_user(user,"You do not have a clone in that room.\n");
}


/*** Set what a clone will hear, either all speach , just bad language
	or nothing. ***/
clone_hear(user)
UR_OBJECT user;
{
RM_OBJECT rm;
UR_OBJECT u;

if (word_count<3  
    || (strcmp(word[2],"all") 
	    && strcmp(word[2],"swears") 
	    && strcmp(word[2],"nothing"))) {
	write_usage(user,"chear <room clone is in> all/swears/nothing\n");
	return;
	}
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE && u->room==rm && u->owner==user) break;
	}
if (u==NULL) {
	write_user(user,"You do not have a clone in that room.\n");
	return;
	}
if (!strcmp(word[2],"all")) {
	u->clone_hear=CLONE_HEAR_ALL;
	write_user(user,"Clone will now hear everything.\n");
	return;
	}
if (!strcmp(word[2],"swears")) {
	u->clone_hear=CLONE_HEAR_SWEARS;
	write_user(user,"Clone will now only hear swearing.\n");
	return;
	}
u->clone_hear=CLONE_HEAR_NOTHING;
write_user(user,"Clone will now hear nothing.\n");
}


/*** Stat a remote system ***/
remote_stat(user)
UR_OBJECT user;
{
NL_OBJECT nl;
RM_OBJECT rm;

if (word_count<2) {
	write_usage(user,"rstat <room service is linked to>\n");  return;
	}
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
if ((nl=rm->netlink)==NULL) {
	write_user(user,"That room is not linked to a service.\n");
	return;
	}
if (nl->stage!=2) {
	write_user(user,"Not (fully) connected to service.\n");
	return;
	}
if (nl->ver_major<=3 && nl->ver_minor<1) {
	write_user(user,"The NUTS version running that service does not support this facility.\n");
	return;
	}
sprintf(text,"RSTAT %s\n",user->name);
write_sock(nl->socket,text);
write_user(user,"Request sent.\n");
}


/*** Switch swearing ban on and off ***/
swban(user)
UR_OBJECT user;
{
if (!ban_swearing) {
	write_user(user,"Swearing ban ~FGon.\n");
	sprintf(text,"%s switched swearing ban on.\n",user->name);
	write_syslog(text,1);
	sprintf(text,"[ %s(%s) switched swearing ban on. ]\n",user->name,levels[user->level]);
	write_level(ARCH,1,text,NULL);
	ban_swearing=1;  return;
	}
write_user(user,"Swearing ban ~FRoff.\n");
sprintf(text,"%s switched swearing ban off.\n",user->name);
write_syslog(text,1);
sprintf(text,"[ %s(%s) switched swearing ban off. ]\n",user->name,levels[user->level]);
write_level(ARCH,1,text,NULL);
ban_swearing=0;
}


/*** Do AFK ***/
afk(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
if (word_count>1) {
	if (!strcmp(word[1],"lock")) {
		if (user->type==REMOTE_TYPE) {
			/* This is because they might not have a local account and hence
			   they have no password to use. */
			write_user(user,"Sorry, due to software limitations remote users cannot use the lock option.\n");
			return;
			}
		inpstr=remove_first(inpstr);
		if (strlen(inpstr)>AFK_MESG_LEN) {
			write_user(user,"AFK message too long.\n");  return;
			}
		write_user(user,"You are now AFK with the session locked, enter your password to unlock it.\n");
		if (inpstr[0]) {
			strcpy(user->afk_mesg,inpstr);
			write_user(user,"AFK message set.\n");
			}
		user->afk=2;
		}
	else {
		if (strlen(inpstr)>AFK_MESG_LEN) {
			write_user(user,"AFK message too long.\n");  return;
			}
		write_user(user,"You are now AFK, press <return> to reset.\n");
		if (inpstr[0]) {
			strcpy(user->afk_mesg,inpstr);
			write_user(user,"AFK message set.\n");
			}
		user->afk=1;
		}
	}
else {
	write_user(user,"You are now AFK, press <return> to reset.\n");
	user->afk=1;
	}
if (user->vis) {
	if (user->afk_mesg[0]) 
		sprintf(text,"%s goes AFK: %s\n",user->name,user->afk_mesg);
	else sprintf(text,"%s goes AFK...\n",user->name);
	write_room_except(user->room,text,user);
	}
}


/*** Toggle user colour on and off ***/
toggle_colour(user)
UR_OBJECT user;
{
int col;

/* A hidden "feature" , notalot of practical use but lets see if any users
   stumble across it :) */
if (user->command_mode && user->ignall && user->charmode_echo) {
	for(col=1;col<NUM_COLS;++col) {
		sprintf(text,"%s: ~%sNUTS 3 VIDEO TEST~RS\n",colcom[col],colcom[col]);
		write_user(user,text);
		}
	return;
	}
if (user->colour) {
	write_user(user,"Colour ~FRoff.\n");  
	user->colour=0;
	}
else {
	user->colour=1;  
	write_user(user,"Colour ~FGon.\n");
	}
if (user->room==NULL) prompt(user);
}


toggle_ignshout(user)
UR_OBJECT user;
{
if (user->ignshout) {
	write_user(user,"You are no longer ignoring shouts and shout emotes.\n");  
	user->ignshout=0;
	return;
	}
write_user(user,"You are now ignoring shouts and shout emotes.\n");
user->ignshout=1;
}


toggle_igntell(user)
UR_OBJECT user;
{
if (user->igntell) {
	write_user(user,"You are no longer ignoring tells and private emotes.\n");  
	user->igntell=0;
	return;
	}
write_user(user,"You are now ignoring tells and private emotes.\n");
user->igntell=1;
}

toggle_ignchat(user)
UR_OBJECT user;
{
if (user->ignchat) {
	write_user(user,"You are no longer ignoring the chat channel or emotes.\n");  
	user->ignchat=0;
	return;
	}
write_user(user,"You are now ignoring the chat channel or emotes.\n");
user->ignchat=1;
}

toggle_ignwiz(user)
UR_OBJECT user;
{
if (user->ignwiz) {
	write_user(user,"You are no longer ignoring the Wiz channel or emotes.\n");  
	user->ignwiz=0;
	return;
	}
write_user(user,"You are now ignoring the Wiz channel or emotes.\n");
user->ignwiz=1;
}

toggle_ignhonor(user)
UR_OBJECT user;
{
if (user->ignwiz) {
	write_user(user,"You are no longer ignoring the Honor channel or emotes.\n");  
	user->ignhonor=0;
	return;
	}
write_user(user,"You are now ignoring the Honor channel or emotes.\n");
user->ignhonor=1;
}

toggle_ignadmin(user)
UR_OBJECT user;
{
if (user->ignadmin) {
	write_user(user,"You are no longer ignoring the Admin channel or emotes.\n");  
	user->ignadmin=0;
	return;
	}
write_user(user,"You are now ignoring the Admin channel or emotes.\n");
user->ignadmin=1;
}

toggle_igngod(user)
UR_OBJECT user;
{
if (user->igngod) {
	write_user(user,"You are no longer ignoring the God channel or emotes.\n");  
	user->igngod=0;
	return;
	}
write_user(user,"You are now ignoring the God channel or emotes.\n");
user->igngod=1;
}

suicide(user)
UR_OBJECT user;
{
if (word_count<2) {
	write_usage(user,"suicide <your password>\n");  return;
	}
if (strcmp((char *)crypt(word[1],"NU"),user->pass)) {
	write_user(user,"Password incorrect.\n");  return;
	}
write_user(user,"\n\07~FR~OL~LI*** WARNING - This will delete your account! ***\n\nAre you sure about this (y/n)? ");
user->misc_op=6;  
no_prompt=1;
}


/*** Delete a user ***/
delete_user(user,this_user)
UR_OBJECT user;
int this_user;
{
UR_OBJECT u;
char filename[80],name[USER_NAME_LEN+1];

if (this_user) {
	/* User structure gets destructed in disconnect_user(), need to keep a
	   copy of the name */
	strcpy(name,user->name); 
	write_user(user,"\n~FR~LI~OLACCOUNT DELETED!\n");
	sprintf(text,"~OL~LI%s commits suicide!\n",user->name);
	write_room_except(user->room,text,user);
	sprintf(text,"%s SUICIDED.\n",name);
	write_syslog(text,1);
	sprintf(text,"[ %s(%s) suicided. ]\n",user->name,levels[user->level]);
	write_level(ARCH,1,text,NULL);
	disconnect_user(user);
	sprintf(filename,"%s/%s.D",USERFILES,name);
	unlink(filename);
	sprintf(filename,"%s/%s.M",USERFILES,name);
	unlink(filename);
	sprintf(filename,"%s/%s.P",USERFILES,name);
	unlink(filename);
	return;
	}
if (word_count<2) {
	write_usage(user,"delete <user>\n");  return;
	}
word[1][0]=toupper(word[1][0]);
if (!strcmp(word[1],user->name)) {
	write_user(user,"Trying to delete yourself is the eleventh sign of madness.\n");
	return;
	}
if (get_user(word[1])!=NULL) {
	/* Safety measure just in case. Will have to .kill them first */
	write_user(user,"You cannot delete a user who is currently logged on.\n");
	return;
	}
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user object.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user object in delete_user().\n",0);
	return;
	}
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);  
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->level>=user->level) {
	write_user(user,"You cannot delete a user of an equal or higher level than yourself.\n");
	destruct_user(u);
	destructed=0;
	return;
	}
destruct_user(u);
destructed=0;
sprintf(filename,"%s/%s.D",USERFILES,word[1]);
unlink(filename);
sprintf(filename,"%s/%s.M",USERFILES,word[1]);
unlink(filename);
sprintf(filename,"%s/%s.P",USERFILES,word[1]);
unlink(filename);
if (user->vis) sprintf(text,"\07~FR~OL~LI%s nukes %s to a crisp...TOASTY!!\n",user->name,word[1]);
sprintf(text,"%s(%s) DELETED %s.\n",user->name,levels[user->level],word[1]);
write_syslog(text,1);
sprintf(text,"[ %s(%s) has deleted %s from the system. ]\n",user->name,levels[user->level],word[1]);
write_level(ARCH,1,text,NULL);
}


/*** Shutdown talker interface func. Countdown time is entered in seconds so
	we can specify less than a minute till reboot. ***/
shutdown_com(user)
UR_OBJECT user;
{
if (rs_which==1) {
	write_user(user,"The reboot countdown is currently active, you must cancel it first.\n");
	return;
	}
if (!strcmp(word[1],"cancel")) {
	if (!rs_countdown || rs_which!=0) {
		write_user(user,"The shutdown countdown is not currently active.\n");
		return;
		}
	if (rs_countdown && !rs_which && rs_user==NULL) {
		write_user(user,"Someone else is currently setting the shutdown countdown.\n");
		return;
		}
	write_room(NULL,"[ ~OLSYSTEM:~RS~FG Shutdown cancelled. ]\n");
	sprintf(text,"%s cancelled the shutdown countdown.\n",user->name);
	write_syslog(text,1);
	sprintf(text,"[ %s(%s) cancelled the shutdown countdown. ]\n",user->name,levels[user->level]);
	write_level(ARCH,1,text,NULL);
	rs_countdown=0;
	rs_announce=0;
	rs_which=-1;
	rs_user=NULL;
	return;
	}
if (word_count>1 && !isnumber(word[1])) {
	write_usage(user,"shutdown [<secs>/cancel]\n");  return;
	}
if (rs_countdown && !rs_which) {
	write_user(user,"The shutdown countdown is currently active, you must cancel it first.\n");
	return;
	}
if (word_count<2) {
	rs_countdown=0;  
	rs_announce=0;
	rs_which=-1; 
	rs_user=NULL;
	}
else {
	rs_countdown=atoi(word[1]);
	rs_which=0;
	}
write_user(user,"\n\07~FR~OL~LI*** WARNING - This will shutdown the talker! ***\n\nAre you sure about this (y/n)? ");
user->misc_op=1;  
no_prompt=1;  
}


/*** Reboot talker interface func. ***/
reboot_com(user)
UR_OBJECT user;
{
if (!rs_which) {
	write_user(user,"The shutdown countdown is currently active, you must cancel it first.\n");
	return;
	}
if (!strcmp(word[1],"cancel")) {
	if (!rs_countdown) {
		write_user(user,"The reboot countdown is not currently active.\n");
		return;
		}
	if (rs_countdown && rs_user==NULL) {
		write_user(user,"Someone else is currently setting the reboot countdown.\n");
		return;
		}
	write_room(NULL,"[ ~OLSYSTEM:~RS~FG Reboot cancelled. ]\n");
	sprintf(text,"%s cancelled the reboot countdown.\n",user->name);
	write_syslog(text,1);
	sprintf(text,"[ %s(%s) cancelled the reboot countdown. ]\n",user->name,levels[user->level]);
	write_level(ARCH,1,text,NULL);
	rs_countdown=0;
	rs_announce=0;
	rs_which=-1;
	rs_user=NULL;
	return;
	}
if (word_count>1 && !isnumber(word[1])) {
	write_usage(user,"reboot [<secs>/cancel]\n");  return;
	}
if (rs_countdown) {
	write_user(user,"The reboot countdown is currently active, you must cancel it first.\n");
	return;
	}
if (word_count<2) {
	rs_countdown=0;  
	rs_announce=0;
	rs_which=-1; 
	rs_user=NULL;
	}
else {
	rs_countdown=atoi(word[1]);
	rs_which=1;
	}
write_user(user,"\n\07~FY~OL~LI*** WARNING - This will reboot the talker! ***\n\nAre you sure about this (y/n)? ");
user->misc_op=7;  
no_prompt=1;  
}

/*** Show recorded tells and pemotes ***/
revtell(user)
UR_OBJECT user;
{
int i,cnt,line;

cnt=0;
for(i=0;i<REVTELL_LINES;++i) {
	line=(user->revline+i)%REVTELL_LINES;
	if (user->revbuff[line][0]) {
		cnt++;
		if (cnt==1) write_user(user,"\n~BB~FGYour revtell buffer:\n\n");
		write_user(user,user->revbuff[line]); 
		}
	}
if (!cnt) write_user(user,"Revtell buffer is empty.\n");
else write_user(user,"\n~BB~FG*** End ***\n\n");
}



/**************************** EVENT FUNCTIONS ******************************/

void do_events()
{
set_date_time();
check_reboot_shutdown();
check_idle_and_timeout();
check_nethangs_send_keepalives(); 
check_messages(NULL,0);
reset_alarm();
}


reset_alarm()
{
signal(SIGALRM,do_events);
alarm(heartbeat);
}



/*** See if timed reboot or shutdown is underway ***/
check_reboot_shutdown()
{
int secs;
char *w[]={ "~FRShutdown","~FYRebooting" };

if (rs_user==NULL) return;
rs_countdown-=heartbeat;
if (rs_countdown<=0) talker_shutdown(rs_user,NULL,rs_which);

/* Print countdown message every minute unless we have less than 1 minute
   to go when we print every 10 secs */
secs=(int)(time(0)-rs_announce);
if (rs_countdown>=60 && secs>=60) {
	sprintf(text,"[ ~OLSYSTEM: %s in %d minutes, %d seconds. ]\n",w[rs_which],rs_countdown/60,rs_countdown%60);
	write_room(NULL,text);
	rs_announce=time(0);
	}
if (rs_countdown<60 && secs>=10) {
	sprintf(text,"[ ~OLSYSTEM: %s in %d seconds. ]\n",w[rs_which],rs_countdown);
	write_room(NULL,text);
	rs_announce=time(0);
	}
}



/*** login_time_out is the length of time someone can idle at login, 
     user_idle_time is the length of time they can idle once logged in. 
     Also ups users total login time. ***/
check_idle_and_timeout()
{
UR_OBJECT user,next;
int tm;

/* Use while loop here instead of for loop for when user structure gets
   destructed, we may lose ->next link and crash the program */
user=user_first;
while(user) {
	next=user->next;
	if (user->type==CLONE_TYPE) {  user=next;  continue;  }
	user->total_login+=heartbeat; 
	if (user->level>time_out_maxlevel) {  user=next;  continue;  }

	tm=(int)(time(0) - user->last_input);
	if (user->login && tm>=login_idle_time) {
		write_user(user,"\n\n*** Time out ***\n\n");
		disconnect_user(user);
		user=next;
		continue;
		}
	if (user->warned) {
		if (tm<user_idle_time-60) {  user->warned=0;  continue;  }
		if (tm>=user_idle_time) {
			write_user(user,"\n\n\07~FR~OL~LI*** You have been timed out. ***\n\n");
			disconnect_user(user);
			user=next;
			continue;
			}
		}
	if ((!user->afk || (user->afk && time_out_afks)) 
	    && !user->login 
	    && !user->warned
	    && tm>=user_idle_time-60) {
		write_user(user,"\n\07~FY~OL~LI*** WARNING - Input within 1 minute or you will be disconnected. ***\n\n");
		user->warned=1;
		}
	user=next;
	}
}
	


/*** See if any net connections are dragging their feet. If they have been idle
     longer than net_idle_time the drop them. Also send keepalive signals down
     links, this saves having another function and loop to do it. ***/
check_nethangs_send_keepalives()
{
NL_OBJECT nl;
int secs;

for(nl=nl_first;nl!=NULL;nl=nl->next) {
	if (nl->type==UNCONNECTED) {
		nl->warned=0;  continue;
		}

	/* Send keepalives */
	nl->keepalive_cnt+=heartbeat;
	if (nl->keepalive_cnt>=keepalive_interval) {
		write_sock(nl->socket,"KA\n");
		nl->keepalive_cnt=0;
		}

	/* Check time outs */
	secs=(int)(time(0) - nl->last_recvd);
	if (nl->warned) {
		if (secs<net_idle_time-60) nl->warned=0;
		else {
			if (secs<net_idle_time) continue;
			sprintf(text,"[ ~OLSYSTEM:~RS Disconnecting hung netlink to %s in the %s. ]\n",nl->service,nl->connect_room->name);
			write_room(NULL,text);
			shutdown_netlink(nl);
			nl->warned=0;
			}
		continue;
		}
	if (secs>net_idle_time-60) {
		sprintf(text,"[ ~OLSYSTEM:~RS Netlink to %s in the %s has been hung for %d seconds. ]\n",nl->service,nl->connect_room->name,secs);
		write_level(ARCH,1,text,NULL);
		nl->warned=1;
		}
	}
destructed=0;
}



/*** Remove any expired messages from boards unless force = 2 in which case
	just do a recount. ***/
check_messages(user,force)
UR_OBJECT user;
int force;
{
RM_OBJECT rm;
FILE *infp,*outfp;
char id[82],filename[80],line[82];
int valid,pt,write_rest;
int board_cnt,old_cnt,bad_cnt,tmp;
static int done=0;

switch(force) {
	case 0:
	if (mesg_check_hour==thour && mesg_check_min==tmin) {
		if (done) return;
		}
	else {  done=0;  return;  }
	break;

	case 1:
	printf("Checking boards...\n");
	}
done=1;
board_cnt=0;
old_cnt=0;
bad_cnt=0;

for(rm=room_first;rm!=NULL;rm=rm->next) {
	tmp=rm->mesg_cnt;  
	rm->mesg_cnt=0;
	sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
	if (!(infp=fopen(filename,"r"))) continue;
	if (force<2) {
		if (!(outfp=fopen("tempfile","w"))) {
			if (force) fprintf(stderr,"NUTS: Couldn't open tempfile.\n");
			write_syslog("ERROR: Couldn't open tempfile in check_messages().\n",0);
			fclose(infp);
			return;
			}
		}
	board_cnt++;
	/* We assume that once 1 in date message is encountered all the others
	   will be in date too , hence write_rest once set to 1 is never set to
	   0 again */
	valid=1; write_rest=0;
	fgets(line,82,infp); /* max of 80+newline+terminator = 82 */
	while(!feof(infp)) {
		if (*line=='\n') valid=1;
		sscanf(line,"%s %d",id,&pt);
		if (!write_rest) {
			if (valid && !strcmp(id,"PT:")) {
				if (force==2) rm->mesg_cnt++;
				else {
					/* 86400 = num. of secs in a day */
					if ((int)time(0) - pt < mesg_life*86400) {
						fputs(line,outfp);
						rm->mesg_cnt++;
						write_rest=1;
						}
					else old_cnt++;
					}
				valid=0;
				}
			}
		else {
			fputs(line,outfp);
			if (valid && !strcmp(id,"PT:")) {
				rm->mesg_cnt++;  valid=0;
				}
			}
		fgets(line,82,infp);
		}
	fclose(infp);
	if (force<2) {
		fclose(outfp);
		unlink(filename);
		if (!write_rest) unlink("tempfile");
		else rename("tempfile",filename);
		}
	if (rm->mesg_cnt!=tmp) bad_cnt++;
	}
switch(force) {
	case 0:
	if (bad_cnt) 
		sprintf(text,"CHECK_MESSAGES: %d files checked, %d had an incorrect message count, %d messages deleted.\n",board_cnt,bad_cnt,old_cnt);
	else sprintf(text,"CHECK_MESSAGES: %d files checked, %d messages deleted.\n",board_cnt,old_cnt);
	write_syslog(text,1);
	break;

	case 1:
	printf("  %d board files checked, %d out of date messages found.\n",board_cnt,old_cnt);
	break;

	case 2:
	sprintf(text,"%d board files checked, %d had an incorrect message count.\n",board_cnt,bad_cnt);
	write_user(user,text);
	sprintf(text,"%s forced a recount of the message boards.\n",user->name);
	write_syslog(text,1);
	sprintf(text,"[ %s(%s) forced a recount of the message boards. ]\n",user->name,levels[user->level]);
	write_level(ARCH,1,text,NULL);
	}
}

/**************************** Made in Canada *******************************/
