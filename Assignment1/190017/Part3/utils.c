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

void spawnTeams(void) {
	// a process for each team
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

			// set final teams
			if (groupNo == 0) {
				assert(read(groupPipe[groupNo][0], &finalTeam1, sizeof(int)) ==
						sizeof(int));
			} else {
				assert(read(groupPipe[groupNo][0], &finalTeam2, sizeof(int)) ==
						sizeof(int));
			}

			// close pipes
			assert(close(groupPipe[groupNo][0]) >= 0);
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
			assert(write(groupPipe[groupNo][1], &groupLeader, sizeof(int)) ==
					sizeof(int));

			// close write end too
			if (close(groupPipe[groupNo][1]) < 0) {
				perror("close");
				exit(-1);
			}

			// self-terminate
			exit(0);
		}
	}
}

void endTeam(int teamID) {
	// write to the write end of commpipe
	// of the team which need to stop
	int signal = 0;
	assert(write(teams[teamID].commpipe[1], &signal, sizeof(int)) == sizeof(int));
}

int match(int team1, int team2) {
	// team1 has lower index than team2

	// do a toss

	// ask for two values
	int p = 1, q = 1;
	assert(write(teams[team1].commpipe[1], &p, sizeof(int)) == sizeof(int));
	assert(write(teams[team2].commpipe[1], &q, sizeof(int)) == sizeof(int));
	// read two values
	int i, j;
	assert(read(teams[team1].matchpipe[0], &i, sizeof(int)) == sizeof(int));
	assert(read(teams[team2].matchpipe[0], &j, sizeof(int)) == sizeof(int));

	// decide toss winner
	int BatTeam = team1, BowlTeam = team2;
	if ((i + j) % 2 == 0) {
		// even
		BatTeam = team2;
		BowlTeam = team1;
	}

	// prepare file Name
	char matchFileName[64] = {'\0'};
	sprintf(matchFileName, "test/%d/out/%sv%s", test, team_names[BatTeam],
			team_names[BowlTeam]);

	// if final: different groups
	if ((team2 - team1) >= 4) {
		strcat(matchFileName, "-Final");
	}

	// file to write the results
	int matchFile = open(matchFileName, O_CREAT | O_RDWR, 0644);

	int winnerTeam = -1;
	int targetScore = -1;
	// Two innings
	for (int i = 1; i <= 2; i++) {

		// Declare start of inning
		char inning[64] = {'\0'};

		sprintf(inning, "Innings%d: %s bats\n", i, team_names[BatTeam]);
		assert(write(matchFile, inning, strlen(inning)) == strlen(inning));

		int batsManId = 1;
		int batsManScore = 0;
		int totalScore = 0;

		// batting team hits, bowling team bowls
		int hit, ball;
		// 20 over inning
		for (int j = 0; j < 120; j++) {
			// ask for two values
			int p = WR, q = WR;
			assert(write(teams[BatTeam].commpipe[1], &p, sizeof(int)) == sizeof(int));
			assert(write(teams[BowlTeam].commpipe[1], &q, sizeof(int)) ==
					sizeof(int));
			// read result
			assert(read(teams[BatTeam].matchpipe[0], &hit, sizeof(int)) ==
					sizeof(int));
			assert(read(teams[BowlTeam].matchpipe[0], &ball, sizeof(int)) ==
					sizeof(int));

			if (hit != ball) {
				totalScore += hit;
				batsManScore += hit;
			} else {
				// wicket down
				char wicket[64] = {'\0'};
				sprintf(wicket, "%d:%d\n", batsManId, batsManScore);
				assert(write(matchFile, wicket, strlen(wicket)) == strlen(wicket));

				// new batsman
				batsManId += 1;
				batsManScore = 0;
			}

			// chasing team reaches the target
			if ((i == 2) && (totalScore > targetScore))
				break;

			// all wicket down
			if (batsManId == 11)
				break;
		}

		// last batsman is not out
		if (batsManId != 11) {
			char wicket[64] = {'\0'};
			sprintf(wicket, "%d:%d*\n", batsManId, batsManScore);
			assert(write(matchFile, wicket, strlen(wicket)) == strlen(wicket));
		}

		// inning summary
		char inningSummary[64] = {'\0'};
		sprintf(inningSummary, "%s TOTAL: %d\n", team_names[BatTeam], totalScore);
		assert(write(matchFile, inningSummary, strlen(inningSummary)) ==
				strlen(inningSummary));
		// swap the order
		int temp = BatTeam;
		BatTeam = BowlTeam;
		BowlTeam = temp;

		// match finished
		if (i == 2) {
			char matchSummary[128] = {'\0'};
			if (totalScore > targetScore) {
				// bowling team wins
				winnerTeam = BowlTeam;
				sprintf(matchSummary, "%s beats %s by %d wickets\n",
						team_names[BowlTeam], team_names[BatTeam], 11 - batsManId);
			} else if (totalScore == targetScore) {
				// tie: lower index team wins
				winnerTeam = team1;
				sprintf(matchSummary, "TIE: %s beats %s\n", team_names[team1],
						team_names[team2]);
			} else {
				// batting team wins
				winnerTeam = BatTeam;
				sprintf(matchSummary, "%s beats %s by %d runs\n", team_names[BatTeam],
						team_names[BowlTeam], targetScore - totalScore);
			}

			assert(write(matchFile, matchSummary, strlen(matchSummary)) ==
					strlen(matchSummary));
		}

		// target for chasing team
		targetScore = totalScore;
		// prepare for second inning
		batsManScore = 0;
		batsManId = 1;
		totalScore = 0;
	}

	assert(close(matchFile) >= 0);
	return winnerTeam;
}

void teamPlay(void) {
	// process type defines the index

	// prepare the path of input directory
	char path[64] = {'\0'};
	sprintf(path, "test/%d/inp/%s", test, team_names[processType]);

	int shotFd = open(path, O_RDONLY);
	if (shotFd < 0) {
		perror("open");
		exit(-1);
	}

	while (1) {
		int signal, shot;
		char hit;
		// wait for request
		assert(read(teams[processType].commpipe[0], &signal, sizeof(int)) ==
				sizeof(int));

		// close request
		if (signal == 0)
			break;

		// fullfill the request
		// 1 character at a time
		assert(read(shotFd, &hit, sizeof(char)) == sizeof(char));
		shot = hit - '0';
		assert(write(teams[processType].matchpipe[1], &shot, sizeof(int)) ==
				sizeof(int));
	}

	// done with this process
	assert(close(shotFd) >= 0);
	assert(close(teams[processType].commpipe[0]) >= 0);
	assert(close(teams[processType].matchpipe[1]) >= 0);
	exit(0);
}
