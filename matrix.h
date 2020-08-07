#ifndef MATRIX_H
#define MATRIX_H

#define M_ELEMENT(matrix, i, j) ((matrix)->data[(i)*(matrix)->columns + (j)])

typedef struct {
    int rows;
    int columns;
    long double *data;
} matrix_str;

/* matrix.c */

/* Rows, Columns */
matrix_str *matrix_create(int , int );
void matrix_delete(matrix_str *);

matrix_str *matrix_add(matrix_str *, matrix_str *);
matrix_str *matrix_sub(matrix_str *, matrix_str *);
matrix_str *matrix_mul(matrix_str *, matrix_str *);

void matrix_swap_rows(matrix_str *, int, int);
void matrix_swap_columns(matrix_str *, int, int);
void matrix_add_rows(matrix_str *, int);

int matrix_copy(matrix_str *, matrix_str *);
int matrix_copy_column(matrix_str *, int, matrix_str *, int);

matrix_str *system_solve_gauss(matrix_str *);

matrix_str *matrix_transpose(matrix_str *);
matrix_str *matrix_reverse(matrix_str *);
long double matrix_determinant(matrix_str *);
matrix_str *matrix_get_own_vector(matrix_str *, long double, long double, int);

void matrix_mul_lambda(matrix_str *, long double);
long double vector_abs_value(matrix_str *);
void matrix_add2(matrix_str *, matrix_str *, long double);
void matrix_sub2(matrix_str *, matrix_str *);

/* Auxiliary debug procedure */
void matrix_print(matrix_str *);

#endif /* MATRIX_H */
