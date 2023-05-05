#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define DO 2
#define KILL 1
#define REQUEST 5
#define SCALE 1000

double a = 0.00001;
double b = 2.5;
double eps = 0.00001;


struct threadData{
	int threadID;
	int id_request;
	double err;
};

struct lineSegment{
	double lhs;
	double rhs;
};

struct msgbuf{
	long mtype;
	double mtext[3];
};

double func (double x) {
        return sin (1.0 / x);
}

double simpson_integration (double lhs, double middle, double rhs) {
        return (rhs - lhs) / 6.0 * (func(lhs) + 4 * func((middle) + func(rhs)));
}

double trapezoid_integration (double lhs, double rhs) {
        return (rhs - lhs) / 2.0 * (func(lhs) + func(rhs));
}

double adaptive_integration (double lhs, double rhs, double integral, double err) {
	double middle = (lhs + rhs) / 2.0;
	double left_lhs = lhs;
	double left_rhs = middle;
	double right_lhs = middle;
	double right_rhs = rhs;
	double left_integral = trapezoid_integration(left_lhs, left_rhs);
	double right_integral = trapezoid_integration(right_lhs, right_rhs);
	//printf("left_left: %f left_right: %f left_integral: %f\n", left_lhs, left_rhs, left_integral);
	//printf("right_left: %f right_right: %f right_integral: %f\n", right_lhs, right_rhs, right_integral);

	if (fabs(integral - (left_integral + right_integral)) >= err) {
		left_integral = adaptive_integration(left_lhs, left_rhs, left_integral, err / 2.0);
		right_integral = adaptive_integration(right_lhs, right_rhs, right_integral, err / 2.0);
		return left_integral + right_integral;
	} else {
		return integral;
	}
}

void *thread_function(void *data) {
	struct msgbuf buf;
	struct threadData *thread_data = (struct threadData *) data;
	int id_request = thread_data->id_request;
	double res = 0.0;

	//printf("Process %d created\n", thread_data->threadID);
        
	buf.mtype = REQUEST;
	buf.mtext[2] = res;
        msgsnd(id_request, &buf, sizeof(struct msgbuf), MSG_NOERROR);
        msgrcv(id_request, &buf, sizeof(struct msgbuf), -3, MSG_NOERROR);

	//printf("Recieve: %ld %f %f\n", buf.mtype, buf.mtext[0], buf.mtext[1]);

	while (buf.mtype != KILL) {	
		clock_t start = clock();
        	double integral = trapezoid_integration(buf.mtext[0], buf.mtext[1]);
        	res = adaptive_integration(buf.mtext[0], buf.mtext[1], integral, thread_data->err);
        	clock_t end = clock();

        	printf("Time: %f proc_count: %d left %f right %f\n", (double) (end - start) / CLOCKS_PER_SEC, thread_data->threadID, buf.mtext[0], buf.mtext[1]);

		buf.mtext[2] = res;
		buf.mtype = REQUEST;
	        msgsnd(id_request, &buf, sizeof(struct msgbuf), MSG_NOERROR);
        	msgrcv(id_request, &buf, sizeof(struct msgbuf), -3, MSG_NOERROR);
	}

        return 0;
}

void dispenser_for_sin(struct lineSegment **arr, int line_count) {
	double d = 2.0;
	double lhs = a;
	double rhs = b;
	int i;

	for (i = 0; i < line_count - 1; i++) {
		arr[i]->lhs = rhs - (rhs - lhs) / d;
		arr[i]->rhs = rhs;
		rhs = rhs - (rhs - lhs) / d;
	}
	arr[i]->lhs = lhs;
	arr[i]->rhs = rhs;

	return;
}

void dispenser(struct lineSegment **arr, int line_count) {
        double lhs = a;
        double rhs = b;
	double step = (rhs - lhs)  / (double) line_count;
        int i;

        for (i = 0; i < line_count; i++) {
                arr[i]->lhs = rhs - step;
                arr[i]->rhs = rhs;
        	rhs = arr[i]->lhs;
        }

        return;
}


double fast_integration(int id_request, int proc_count, double lhs, double rhs, double err){
	struct threadData data[proc_count];
	struct msgbuf buf;
	pthread_t thread[proc_count];
	int i;
	int line_count = proc_count * SCALE;
	double result = 0;

	struct lineSegment **arr = malloc(line_count * sizeof(struct lineSegment *));

	for (i = 0; i < line_count; i++) {
		arr[i] = malloc(1 * sizeof(struct lineSegment));	
	}

	dispenser(arr, line_count); 

	for (i = 0; i < line_count; i++) {
		printf("line_count: %d lhs %f rhs %f\n", line_count, arr[i]->lhs, arr[i]->rhs);
	}

	for (int k = 0; k < line_count; k++) {
		if (k < proc_count) {
			data[k].threadID = k;
			data[k].id_request = id_request;
                        data[k].err = err / sqrt(line_count);

			pthread_create(&thread[k], NULL, thread_function, &data[k]);

		}

		msgrcv(id_request, &buf, sizeof(struct msgbuf), REQUEST, MSG_NOERROR);
		//printf("Result: %f %f %f\n", buf.mtext[0], buf.mtext[1], buf.mtext[2]);
		result = result + buf.mtext[2];
		buf.mtext[0] = arr[k]->lhs;
		buf.mtext[1] = arr[k]->rhs;
		//printf("Send: %f %f\n", buf.mtext[0], buf.mtext[1]);
		buf.mtype = DO;
		msgsnd(id_request, &buf, sizeof(struct msgbuf), MSG_NOERROR);
	}

	for (i = 0; i < proc_count; i++) {
                msgrcv(id_request, &buf, sizeof(struct msgbuf), REQUEST, MSG_NOERROR);
		//printf("Result: %f %f %f\n", buf.mtext[0], buf.mtext[1], buf.mtext[2]);
                result = result + buf.mtext[2];
        }

	for (i = 0; i < proc_count; i++) {
		buf.mtype = KILL;
		msgsnd(id_request, &buf, 0, MSG_NOERROR);
	}

	for (i = 0; i < proc_count; i++) {
                        pthread_join(thread[i], NULL);
	}

	return result;
}

int main (int argn, char **argv) {
	double res = 0.0;
	int id_request = msgget(IPC_PRIVATE, IPC_CREAT | 0777);

	if (argn != 2) {
		printf ("Enter only the number of proccesses\n");
		return 0;
	}

	int proc_count = atoi(argv[1]);

	clock_t start = clock();
	if (proc_count == 1) {
        	double integral = trapezoid_integration(a, b);
        	res = adaptive_integration(a, b, integral, eps);
	} else {
		res = fast_integration(id_request, proc_count, a, b, eps);
	}

	clock_t end = clock();
	printf("Time: %f\n", (double) (end - start) / CLOCKS_PER_SEC);
	printf("Result: %f\n", res);

	msgctl(id_request, IPC_RMID, NULL);

	return 0;
  }
