#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include "utils.h"

#include "matrix.h"

/* Debug procedure */
void matrix_print(matrix_str *matrix)
{
    int i, j;

    printf("\n");
    for(i = 0; i < matrix->rows; i++) {
        for(j = 0; j < matrix->columns; j++)
            printf("%12Lg", M_ELEMENT(matrix, i, j));
        printf("\n");
    }
    printf("\n");
}

matrix_str *matrix_create(int rows, int columns)
{
    matrix_str *matrix;

    if (rows <= 0 || columns <= 0)
        return NULL;

    matrix = (matrix_str *)malloc(sizeof(matrix_str));

    if (matrix) {
        matrix->data = (long double *)calloc(rows*columns, sizeof(long double));
        matrix->rows = rows;
        matrix->columns = columns;
    }

    return matrix;
}

void matrix_delete(matrix_str *matrix)
{
    if(!matrix)
        return;

    if (matrix->data)
        free(matrix->data);

    if (matrix)
        free(matrix);
}

matrix_str *matrix_add(matrix_str *mtr1, matrix_str *mtr2)
{
    matrix_str *mtr_sum;
    int ii;

    if (mtr1->rows != mtr2->rows || mtr2->columns != mtr2->columns)
        return NULL;

    mtr_sum = matrix_create(mtr1->rows, mtr1->columns);

    if (mtr_sum) {
        for (ii = 0; ii < (mtr1->rows)*(mtr2->columns); ii++)
            mtr_sum->data[ii] = mtr1->data[ii] + mtr2->data[ii];
    }

    return mtr_sum;
}

matrix_str *matrix_sub(matrix_str *mtr1, matrix_str *mtr2)
{
    matrix_str *mtr_sub;
    int ii;

    if (mtr1->rows != mtr2->rows || mtr2->columns != mtr2->columns)
        return NULL;

    mtr_sub = matrix_create(mtr1->rows, mtr1->columns);

    if (mtr_sub) {
        for (ii = 0; ii < (mtr1->rows)*(mtr2->columns); ii++)
            mtr_sub->data[ii] = mtr1->data[ii] - mtr2->data[ii];
    }

    return mtr_sub;
}

matrix_str *matrix_mul(matrix_str *mtr1, matrix_str *mtr2)
{
    matrix_str *mtr_mul;
    int i, j, k;

    if (mtr1->columns != mtr2->rows)
        return NULL;

    mtr_mul = matrix_create(mtr1->rows, mtr2->columns);

    for (i = 0; i < mtr1->rows; i++)
        for (j = 0; j < mtr2->columns; j++)
            for (k = 0; k < mtr1->columns; k++)
                M_ELEMENT(mtr_mul, i, j) += M_ELEMENT(mtr1, i, k)*M_ELEMENT(mtr2, k, j);

    return mtr_mul;

}

void matrix_swap_rows(matrix_str *matrix, int row1, int row2)
{
    int ii;

    for (ii = 0; ii < matrix->columns; ii++) {
        long double temp = M_ELEMENT(matrix, row1, ii);

        M_ELEMENT(matrix, row1, ii) = M_ELEMENT(matrix, row2, ii);
        M_ELEMENT(matrix, row2, ii) = temp;
    }
}

void matrix_swap_columns(matrix_str *matrix, int column1, int column2)
{
    int ii;

    for (ii = 0; ii < matrix->rows; ii++) {
        long double temp = M_ELEMENT(matrix, ii, column1);

        M_ELEMENT(matrix, ii, column1) = M_ELEMENT(matrix, ii, column2);
        M_ELEMENT(matrix, ii, column2) = temp;
    }
}

int matrix_copy(matrix_str *matrix_in, matrix_str *matrix_out)
{

    int ii, jj;

    if (!matrix_in)
        return 0;
    if (!matrix_in->data)
        return 0;

    if (!matrix_out)
        return 0;
    if (!matrix_out->data)
        return 0;

    if (matrix_in->rows != matrix_out->rows ||
        matrix_in->columns != matrix_out->columns)
        return 0;

    for ( ii = 0; ii < matrix_in->rows; ii++)
        for ( jj = 0; jj < matrix_in->columns; jj++)
            M_ELEMENT(matrix_out, ii, jj) = M_ELEMENT(matrix_in, ii, jj);

    return 1;
}

matrix_str *system_solve_gauss(matrix_str *matrix_in)
{
    matrix_str *result_vector;
    matrix_str *matrix = matrix_create(matrix_in->rows, matrix_in->columns);
    int i, j, k;

    matrix_copy(matrix_in, matrix);

    if (matrix->columns - matrix->rows != 1)
        return NULL;

    result_vector = matrix_create(matrix->rows, 1);

    if (!result_vector)
        return NULL;

    /* ***  Gauss method *** */

    /* Main element selection */
    for(i = 0; i < matrix->rows; i++) {
        int num = i;
        long double max = M_ELEMENT(matrix, 0, i);

        for(j = i, num = i; j < matrix->rows; j++)
            if( fabs(M_ELEMENT(matrix, j, i)) > max) {
                max = fabs(M_ELEMENT(matrix, j, i));
                num=j;
            }

        if (num != i)
            matrix_swap_rows(matrix, i, num);

    }


    /* Forward stroke */
    for(i = 0; i < matrix->rows; i++) {

        long double d = M_ELEMENT(matrix, i, i);
        for(j = i; j < matrix->columns; j++)
            M_ELEMENT(matrix, i, j) /= d;

        for (k = i + 1; k < matrix->rows; k++) {
            long double c = M_ELEMENT(matrix, k, i);
            for(j = 0; j < matrix->columns; j++)
                M_ELEMENT(matrix, k, j) -= c*M_ELEMENT(matrix, i, j);
        }

    }

    /* determinant check */
    if(matrix_determinant(matrix) == 0.0)
        {
            matrix_delete(result_vector);
            matrix_delete(matrix);
            return NULL;
        }

    /* Reverse stroke */
    for(i = matrix->rows - 1; i >= 0; i--) {

        for (j = i + 1; j < matrix->rows; j++)
            M_ELEMENT(matrix, i, matrix->columns - 1) -=
                M_ELEMENT(matrix, j, matrix->columns - 1)*M_ELEMENT(matrix, i, j);

        M_ELEMENT(matrix, i, matrix->columns - 1) /= M_ELEMENT(matrix, i, i);
    }
    /* *** End *** */

    for(i = 0; i < matrix->rows; i++)
        M_ELEMENT(result_vector, i, 0) = M_ELEMENT(matrix, i, matrix->columns - 1);

    matrix_delete(matrix);

    return result_vector;
}

matrix_str *matrix_transpose(matrix_str *matrix_in)
{
    matrix_str *matrix = matrix_create(matrix_in->rows, matrix_in->columns);
    int ii, jj;

    matrix_copy(matrix_in, matrix);
    if (!matrix)
        return NULL;

    matrix->rows = matrix_in->columns;
    matrix->columns = matrix_in->rows;

    for (ii = 0; ii < matrix->rows; ii++)
        for (jj = 0; jj < matrix->columns; jj++)
            M_ELEMENT(matrix, ii, jj) = M_ELEMENT(matrix_in, jj, ii);


    return matrix;
}

int matrix_copy_column(matrix_str *in, int c_in, matrix_str *out, int c_out)
{
    int ii;

    if (!in || !out || in->columns - 1 < c_in || out->columns - 1 < c_out || in->rows != out->rows)
        return -1;

    for (ii = 0; ii < in->rows; ii++)
        M_ELEMENT(in, ii, c_in) = M_ELEMENT(out, ii, c_out);

    return 0;

}

matrix_str *matrix_reverse(matrix_str *matrix_in)
{
    matrix_str *add;
    matrix_str *B;
    matrix_str *result;
    long double Det;
    int ii, jj;

    if (!matrix_in || (matrix_in->rows != matrix_in->columns))
        return NULL;

    Det = matrix_determinant(matrix_in);
    if (Det == 0.0)
        return NULL;

    add = matrix_create(matrix_in->rows - 1, matrix_in->columns - 1);
    B = matrix_create(matrix_in->rows, matrix_in->columns);

    for (ii = 0; ii < matrix_in->rows; ii++)
        for (jj = 0; jj < matrix_in->columns; jj++) {
            int ii1, jj1;
            int pos = 0;

            for (ii1 = 0; ii1 < matrix_in->rows; ii1++)
                for (jj1 = 0; jj1 < matrix_in->columns; jj1++)
                    if (ii1 != ii && jj1 != jj) add->data[pos++] = M_ELEMENT(matrix_in, ii1, jj1);

            M_ELEMENT(B, ii, jj) = powl((-1.0), ii + jj)*matrix_determinant(add)/Det;
        }

    result = matrix_transpose(B);

    matrix_delete(B);
    matrix_delete(add);

    return result;
}

long double matrix_determinant(matrix_str *matrix_in)
{
    matrix_str *add;
    long double det = 0;
    int jj;

    if (!matrix_in || (matrix_in->rows != matrix_in->columns))
        return -M_PI;

    if (matrix_in->rows == 1 && matrix_in->columns == 1)
        return M_ELEMENT(matrix_in, 0, 0);

    if (matrix_in->rows == 2 && matrix_in->columns == 2)
        return M_ELEMENT(matrix_in, 0, 0)*M_ELEMENT(matrix_in, 1, 1) - M_ELEMENT(matrix_in, 0, 1)*M_ELEMENT(matrix_in, 1, 0);

    add = matrix_create(matrix_in->rows - 1, matrix_in->columns - 1);

    for (jj = 0; jj < matrix_in->columns; jj++) {
        int ii1, jj1;
        int pos = 0;

        for (ii1 = 1; ii1 < matrix_in->rows; ii1++)
            for (jj1 = 0; jj1 < matrix_in->columns; jj1++)
                if (jj1 != jj) add->data[pos++] = M_ELEMENT(matrix_in, ii1, jj1);

        det += powl((-1.0), jj)*M_ELEMENT(matrix_in, 0, jj)*matrix_determinant(add);

    }

    matrix_delete(add);

    return det;
}

matrix_str *matrix_get_own_vector(matrix_str *matrix_in, long double value, long double eps, int max_iterations)
{

    matrix_str *matrix_lE = matrix_create(matrix_in->rows, matrix_in->columns);
    matrix_str *A;
    matrix_str *GAUSS = matrix_create(matrix_in->rows, matrix_in->columns + 1);
    matrix_str *vector_xs, *vector_xs_1;
    int ii, jj, p;


    for (ii = 0; ii < matrix_lE->rows; ii++) M_ELEMENT(matrix_lE, ii, ii) = 1.0*value;
    vector_xs = matrix_create(matrix_in->rows, 1);
    vector_xs_1 = matrix_create(matrix_in->rows, 1);

    M_ELEMENT(vector_xs_1, 0, 0) = 1.0;


    A = matrix_sub(matrix_in, matrix_lE);

    for (ii = 0; ii < A->columns; ii++) matrix_copy_column(GAUSS, ii, A, ii);


    for (ii = 0; ii < max_iterations; ii++) {
        long double xs_mod = 0.0;

        matrix_copy_column(GAUSS, GAUSS->columns - 1, vector_xs_1, 0);

        vector_xs = system_solve_gauss(GAUSS);

        for (jj = 0; jj < vector_xs->rows; jj++)
            xs_mod += M_ELEMENT(vector_xs, jj, 0)*M_ELEMENT(vector_xs, jj, 0);
        xs_mod = sqrt(xs_mod);
        for (jj = 0; jj < vector_xs->rows; jj++)
            M_ELEMENT(vector_xs, jj, 0) /= xs_mod;


        p = 0;
        for (jj = 0; jj < vector_xs->rows; jj++)
            if ( fabs(M_ELEMENT(vector_xs, jj, 0) - M_ELEMENT(vector_xs_1, jj, 0)) < eps )
                p++;

        if (p == vector_xs->rows) break;


        matrix_copy_column(vector_xs_1, 0, vector_xs, 0);
    }

    matrix_delete(matrix_lE);
    matrix_delete(A);
    matrix_delete(GAUSS);
    matrix_delete(vector_xs_1);

    printf("\n");
    return vector_xs;
}

void matrix_mul_lambda(matrix_str *matrix, long double lambda)
{
    int ii;

    for (ii = 0; ii < (matrix->rows)*(matrix->columns); ii++)
        matrix->data[ii] *= lambda;
}

long double vector_abs_value(matrix_str *vector)
{
    int ii;
    long double result = 0.0;

    if (vector->rows != 1 && vector->columns != 1)
        return -1.0;

    if (vector->rows == 1)
        for (ii = 0; ii < vector->columns; ii++)
            result += M_ELEMENT(vector, 0, ii)*M_ELEMENT(vector, 0, ii);
    else
        for (ii = 0; ii < vector->rows; ii++)
            result += M_ELEMENT(vector, ii, 0)*M_ELEMENT(vector, ii, 0);


    return result;
}


void matrix_add2(matrix_str *mtr1, matrix_str *mtr2, long double la)
{
    int ii;

    if (mtr1->rows != mtr2->rows || mtr2->columns != mtr2->columns ||
       !mtr1->data || !mtr2->data)
        return;

    for (ii = 0; ii < (mtr1->rows)*(mtr2->columns); ii++)
        mtr1->data[ii] += la*mtr2->data[ii];

}

void matrix_sub2(matrix_str *mtr1, matrix_str *mtr2)
{
    int ii;

    if (mtr1->rows != mtr2->rows || mtr2->columns != mtr2->columns
       || !mtr1->data || !mtr2->data)
        return;

    for (ii = 0; ii < (mtr1->rows)*(mtr2->columns); ii++)
        mtr1->data[ii] -= mtr2->data[ii];
}

void matrix_add_rows(matrix_str *matrix, int num)
{
    int ii, jj;

    if (!matrix || !matrix->data)
        return;

    matrix->data = (long double*)realloc(matrix->data, (matrix->rows + num)*(matrix->columns)*sizeof(long double));

    matrix->rows += num;

    if (num < 0)
        return;

    for (jj = matrix->rows - num; jj < matrix->rows; jj++)
        for (ii = 0; ii < matrix->columns; ii++)
            M_ELEMENT(matrix, jj, ii) = 0.0;



}
