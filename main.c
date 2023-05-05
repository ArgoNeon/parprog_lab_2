#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define DISPENSER 10
#define DO 2
#define KILL 1
#define REQUEST 5
#define SCALE 1000
#define SCALE_SIN 7

double a = 0.001;
double b = 2.5;
double eps = 0.00000001;
//double eps = 0.000000000001;


struct threadData{
	int threadID;
	int id_request;
};

struct lineSegment{
	double lhs;
	double rhs;
};

struct msgbuf{
	long mtype;
	double mtext[4];
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

	buf.mtype = REQUEST;
	buf.mtext[2] = res;
        msgsnd(id_request, &buf, sizeof(struct msgbuf), MSG_NOERROR);
        msgrcv(id_request, &buf, sizeof(struct msgbuf), -3, MSG_NOERROR);

	while (buf.mtype != KILL) {	
		//clock_t start = clock();
        	double integral = trapezoid_integration(buf.mtext[0], buf.mtext[1]);
        	res = adaptive_integration(buf.mtext[0], buf.mtext[1], integral, buf.mtext[3]);
        	//clock_t end = clock();

        	//printf("Time: %.16lf proc_count: %d left %.16f right %.16lf error %.16lf\n", (double) (end - start) / CLOCKS_PER_SEC, thread_data->threadID, buf.mtext[0], buf.mtext[1], buf.mtext[3]);

		buf.mtext[2] = res;
		buf.mtype = REQUEST;
	        msgsnd(id_request, &buf, sizeof(struct msgbuf), MSG_NOERROR);
        	msgrcv(id_request, &buf, sizeof(struct msgbuf), -3, MSG_NOERROR);
	}

        return 0;
}

void dispenser_for_sin(struct lineSegment **arr, double *arr_err, int line_count) {
	double d = 2.0;
	double lhs = a;
	double rhs = b;
	int i;

	arr_err[0] = eps / d;

	for (i = 0; i < line_count - 1; i++) {
		arr[i]->lhs = rhs - (rhs - lhs) / d;
		arr[i]->rhs = rhs;
		rhs = rhs - (rhs - lhs) / d;
		arr_err[i + 1] = arr_err[i] / d;
	}
	arr[i]->lhs = lhs;
	arr[i]->rhs = rhs;

	return;
}

void dispenser(struct lineSegment **arr, double *arr_err, int line_count) {
        double lhs = a;
        double rhs = b;
	double step = (rhs - lhs)  / (double) line_count;
        int i;
	double err = eps / line_count;

        for (i = 0; i < line_count; i++) {
                arr[i]->lhs = rhs - step;
                arr[i]->rhs = rhs;
        	rhs = arr[i]->lhs;
		arr_err[i] = err;
        }

        return;
}


double fast_integration(int id_request, int proc_count, double lhs, double rhs, double err){
	struct threadData data[proc_count];
	struct msgbuf buf;
	pthread_t thread[proc_count];
	int i;
	double *arr_err;
	int line_count;
	double result = 0;

	if (DISPENSER == 0) {
		line_count = proc_count * SCALE_SIN;
		arr_err = calloc(proc_count * SCALE_SIN, sizeof(double));
	} else {
		line_count = proc_count * SCALE;
		arr_err = calloc(proc_count * SCALE, sizeof(double));
	}

	struct lineSegment **arr = malloc(line_count * sizeof(struct lineSegment *));

	for (i = 0; i < line_count; i++) {
		arr[i] = malloc(1 * sizeof(struct lineSegment));	
	}

	if (DISPENSER == 0) {
		dispenser_for_sin(arr, arr_err, line_count); 
	} else {
		dispenser(arr, arr_err, line_count);
	}
	
	/*
	for (i = 0; i < line_count; i++) {
		printf("line_count: %d lhs %.16lf rhs %.16lf err %.16lf\n", line_count, arr[i]->lhs, arr[i]->rhs, arr_err[i]);
	}
	*/	

	for (int k = 0; k < line_count; k++) {
		if (k < proc_count) {
			data[k].threadID = k;
			data[k].id_request = id_request;

			pthread_create(&thread[k], NULL, thread_function, &data[k]);

		}

		msgrcv(id_request, &buf, sizeof(struct msgbuf), REQUEST, MSG_NOERROR);
		//printf("Result: %.16lf %.16lf %.16lf %.16lf\n", buf.mtext[0], buf.mtext[1], buf.mtext[2], buf.mtext[3]);
		result = result + buf.mtext[2];
		buf.mtext[0] = arr[k]->lhs;
		buf.mtext[1] = arr[k]->rhs;
		buf.mtext[3] = arr_err[k];
		buf.mtype = DO;
		msgsnd(id_request, &buf, sizeof(struct msgbuf), MSG_NOERROR);
	}

	for (i = 0; i < proc_count; i++) {
                msgrcv(id_request, &buf, sizeof(struct msgbuf), REQUEST, MSG_NOERROR);
		//printf("Result: %.16lf %.16lf %.16lf %.16lf\n", buf.mtext[0], buf.mtext[1], buf.mtext[2], buf.mtext[3]);
                result = result + buf.mtext[2];
        }

	for (i = 0; i < proc_count; i++) {
		buf.mtype = KILL;
		msgsnd(id_request, &buf, 0, MSG_NOERROR);
	}

/*	for (i = 0; i < proc_count; i++) {
                        pthread_join(thread[i], NULL);
	}
*/
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

	//clock_t start = clock();
	if (proc_count == 1) {
        	double integral = trapezoid_integration(a, b);
        	res = adaptive_integration(a, b, integral, eps);
	} else {
		res = fast_integration(id_request, proc_count, a, b, eps);
	}

	//clock_t end = clock();
	//printf("Time: %lf\n", (double) (end - start) / CLOCKS_PER_SEC);
	printf("Result: %.10lf\n", res);

	msgctl(id_request, IPC_RMID, NULL);

	return 0;
  }
