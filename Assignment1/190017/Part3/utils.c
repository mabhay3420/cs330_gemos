#include "wc.h"

extern struct team teams[NUM_TEAMS];
extern int test;
extern int finalTeam1;
extern int finalTeam2;

int processType = HOST;
const char *team_names[] = {
	"India",    "Australia",    "New Zealand", "Sri Lanka", // Group A
	"Pakistan", "South Africa", "England",     "Bangladesh" // Group B
};

#define WKT "7"
#define TERMINATE "8"

void spawnTeams(void) {
	// a process for each tem
	for (int i = 0; i < 8; i++) {
		// assign name
		strcpy(teams[i].name, team_names[i]);

		// setup pipes
		if (pipe(teams[i].commpipe) < 0) {
			perror("pipe");
			exit(-1);
		}
		if (pipe(teams[i].matchpipe) < 0) {
			perror("pipe");
			exit(-1);
		}
	} // setup complete

	// fork team processes
	for (int i = 0; i < 8; i++) {
		processType = i;
		pid_t cpid;

		cpid = fork();
		if (cpid < 0) {
			perror("fork");
			exit(-1);
		}

		if (cpid != 0) {
			// parent process
			processType = HOST;

			// host uses matchpipe for reading and
			// commpipe for writing, so close others.

			if (close(teams[i].matchpipe[1]) < 0) {
				perror("close");
				exit(-1);
			}

			if (close(teams[i].commpipe[0]) < 0) {
				perror("close");
				exit(-1);
			}
		} else {
			// child processes

			// teams use matchpipe for writing data and
			// commpipe for reading, so close others
			if (close(teams[i].matchpipe[0]) < 0) {
				perror("close");
				exit(-1);
			}
			if (close(teams[i].commpipe[1]) < 0) {
				perror("close");
				exit(-1);
			}
			// do not write to commpipe
			teamPlay();
		}
	}
}

void conductGroupMatches(void) {
	// setup pipe for groups

	int groupPipe[2][2];
	for (int groupNo = 0; groupNo < 2; groupNo++) {
		// setup pipes for group
		if (pipe(groupPipe[groupNo]) < 0) {
			perror("pipe");
			exit(-1);
		}

		// create group process
		pid_t cpid;
		cpid = fork();

		if (cpid < 0) {
			perror("fork");
			exit(-1);
		}

		if (cpid != 0) {
			// host process

			// only read from group pipes: seems unnecessary
			// but just for completeness of logic

			if (close(groupPipe[groupNo][1]) < 0) {
				perror("close");
				exit(-1);
			}
		} else {
			// group processes

			// do not read from groupPipe
			if (close(groupPipe[groupNo][0]) < 0) {
				perror("fork");
				exit(-1);
			}

			// non-local processing will cosider 0..7 teams
			// locally we will track only 0..4 teams
			// non-local = local + group_no * 4 and vice versa

			int pointTable[4] = {0};
			int firstTeam, secondTeam, matchWinner;
			for (int i = 0; i < 4; i++) {
				for (int j = i + 1; j < 4; j++) {

					// prepare for non-local processing
					firstTeam = i + groupNo * 4;
					secondTeam = j + groupNo * 4;

					// non-local processing
					matchWinner = match(firstTeam, secondTeam) - groupNo * 4;
					pointTable[matchWinner] += 1;
				}
			}

			int groupLeader;
			for (int i = 0, scoreLeader = -1; i < 4; i++) {
				// lower index element gets priority so no inequality
				if (pointTable[i] > scoreLeader) {
					scoreLeader = pointTable[i];
					groupLeader = i;
				}
			}

			// terminate all non-winning teams
			for (int i = 0; i < 4; i++) {
				if (i != groupLeader) {
					// non-local processing
					endTeam(i + groupNo * 4);
				}
			}

			// communicate to host process

			// prepare for non-local processing
			groupLeader += groupNo * 4;

			// 32-bit integer: 4 bytes
			assert(write(groupPipe[groupNo][1], &groupLeader, 4) == 4);

			// self-terminate
			exit(0);
		}
	}
}
void teamPlay(void) {}

void endTeam(int teamID) {
	// write to the write end of commpipe
	// of the team which need to stop
	assert(write(teams[teamID].commpipe[1], TERMINATE, 2) == 2);
}

int match(int team1, int team2) {
	// team1 has lower index than team2

	// do a toss

	// read two values
	int i, j;
	assert(read(teams[team1].matchpipe[0], &i, 4) == 4);
	assert(read(teams[team2].matchpipe[0], &j, 4) == 4);

	// decide toss winner
	int BatTeam = team1, BowlTeam = team2;
	if ((i + j) % 2 == 0) {
		// even
		BatTeam = team2;
		BowlTeam = team1;
	}

	// prepare file Name
	char matchFileName[64] = "\0";
	sprintf(matchFileName,"%sv%s",team_names[BatTeam],team_names[BowlTeam]);

	// if final: different groups
	if((team2 - team1)>=4){
		strcat(matchFileName,"-Final");
	}
	
	// file to write the results
	int matchFile = open(matchFileName,O_CREAT | O_RDWR,0644);


	int winnerTeam = -1;
	int targetScore = -1;
	// Two innings
	for(int i = 1;i<=2;i++){

		// Declare start of inning
		char inning[64] = "\0";
		sprintf(inning,"Innings%d: %s bats",i,team_names[BatTeam]);
		int byteCount = strlen(inning);
		assert(write(matchFile,inning,byteCount) == byteCount);

		int batsManId = 1;
		int batsManScore = 0;
		int totalScore = 0;

		// batting team hits, bowling team bowls
		int hit,ball;
		// 20 over inning
		for(int j = 0;j<120;j++){
			// assume integer size is 4 bytes in the machine
			assert(read(teams[BatTeam].matchpipe[0],&hit,4)==4);
			assert(read(teams[BowlTeam].matchpipe[0],&ball,4)==4);

			if(hit != ball){
				totalScore += hit;
				batsManScore += hit;
			}
			else{
				// wicket down
				char wicket[64] = "\0";
				sprintf(wicket,"\n%d:%d",batsManId,batsManScore);
				byteCount = strlen(wicket);
				assert(write(matchFile,wicket,byteCount)==byteCount);

				// new batsman
				batsManId += 1;
				batsManScore = 0;
			}

			// chasing team reaches the target
			if((i==2) && (totalScore >= targetScore)) break;

			// all wicket down
			if( batsManId == 11) break;
		}

		// last batsman is not out
		if(batsManId != 11){
			char wicket[64] = "\0";
			sprintf(wicket,"\n%d:%d*",batsManId,batsManScore);
			byteCount = strlen(wicket);
			assert(write(matchFile,wicket,byteCount)==byteCount);
		}

		// target for chasing team
		targetScore = totalScore; 

		// inning summary
		char inningSummary[64] = "\0";
		sprintf(inningSummary,"\n%s Total: %d",team_names[BatTeam],totalScore);
		byteCount = strlen(inning);
		assert(write(matchFile,inningSummary,byteCount) == byteCount);

		// swap the order
		int temp = BatTeam;
		BatTeam = BowlTeam;
		BowlTeam = temp;


		// match finished
		if(i==2){
			char matchSummary[64] = "\0";
			if(totalScore > targetScore){
				// bowling team wins
				winnerTeam = BowlTeam;
				sprintf(matchSummary,"\n%s beats %s by %d wickets",team_names[BowlTeam],team_names[BatTeam],11 - batsManId);
			}
			else if(totalScore == targetScore){
				// tie: lower index team wins
				winnerTeam = team1;
				sprintf(matchSummary,"\n%s beats %s",team_names[team1],team_names[team2]);
			}
			else{
				// batting team wins
				winnerTeam = BatTeam;
				sprintf(matchSummary,"\n%s beats %s by %d runs",team_names[BatTeam],team_names[BowlTeam],targetScore-totalScore);
			}

			byteCount = strlen(matchSummary);
			assert(write(matchFile,matchSummary,byteCount) == byteCount);
		}
	}

	assert(close(matchFile)>=0);
	return winnerTeam;
}
