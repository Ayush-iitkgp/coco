/**
 * @file  c_linear.c
 * @brief Implementation of problems with linear constraints for the
 *        suite of constrained problems.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "coco.h"
#include "coco_internal.h"

/**
 * @brief Data type for the linear constraints.
 */
typedef struct {
  double *gradient;
  double *x;
} linear_constraint_data_t;	

static void c_sum_variables_evaluate(coco_problem_t *self, 
                                     const double *x, 
                                     double *y);
                                     
static void c_linear_single_evaluate(coco_problem_t *self, 
                                     const double *x, 
                                     double *y);
                                        
static coco_problem_t *c_guarantee_feasible_point(coco_problem_t *problem,
                                                  const double *feasible_point);
                                               
static void c_linear_gradient_free(void *thing);

static coco_problem_t *c_sum_variables_allocate(const size_t number_of_variables);

static coco_problem_t *c_linear_transform(coco_problem_t *inner_problem, 
                                          const double *gradient);
         
static coco_problem_t *c_linear_shuffle(coco_problem_t *problem_c, 
                                        linear_constraint_data_t *data_c1);
                                                                                    
double randn(double mu, double sigma);
                                                   
static coco_problem_t *c_linear_single_cons_bbob_problem_allocate(const size_t function,
                                                      const size_t dimension,
                                                      const size_t instance,
                                                      const size_t constraint_number,
                                                      const double linear_cons_norm,
                                                      const char *problem_id_template,
                                                      const char *problem_name_template,
                                                      double *gradient,
                                                      const double *feasible_direction);
                                                      
static coco_problem_t *c_linear_cons_bbob_problem_allocate(const size_t function,
                                                      const size_t dimension,
                                                      const size_t instance,
                                                      const size_t number_of_linear_constraints,
                                                      const double linear_cons_norm,
                                                      const char *problem_id_template,
                                                      const char *problem_name_template,
                                                      const double *feasible_direction);

/**
 * @brief Evaluates the linear constraint with all-ones gradient at
 *        the point 'x' and stores the result into 'y'.
 */
static void c_sum_variables_evaluate(coco_problem_t *self, 
                                     const double *x, 
                                     double *y) {
	
  size_t i;

  assert(self->number_of_constraints == 1);
  
  y[0] = 0.0;
  for (i = 0; i < self->number_of_variables; ++i)
    y[0] += x[i];
}	

/**
 * @brief Evaluates the linear constraint at the point 'x' and stores
 *        the result in 'y'.
 */
static void c_linear_single_evaluate(coco_problem_t *self, 
                                     const double *x, 
                                     double *y) {
	
  size_t i;
  
  linear_constraint_data_t *data;
  coco_problem_t *inner_problem;
  
  data = (linear_constraint_data_t *) coco_problem_transformed_get_data(self);
  inner_problem = coco_problem_transformed_get_inner_problem(self);
  
  assert(self->number_of_constraints == 1);
			
  for (i = 0; i < self->number_of_variables; ++i)
    data->x[i] = (data->gradient[i])*x[i];
  
  coco_evaluate_constraint(inner_problem, data->x, y);
  
  inner_problem = NULL;
  data = NULL;
}

/**
 * @brief Guarantees that "feasible_direction" is feasible w.r.t. 
 *        the constraint in "problem" and records it as the 
 *        initial feasible solution to this coco_problem.
 */
static coco_problem_t *c_guarantee_feasible_point(coco_problem_t *problem,
                                                const double *feasible_direction) {
  
  size_t i;
  linear_constraint_data_t *data;
  double constraint_value = 0.0;
  
  data = (linear_constraint_data_t *) coco_problem_transformed_get_data(problem);
  
  assert(problem->number_of_constraints == 1);
  
  /* Let p be the gradient of the constraint in "problem".
   * Check whether p' * (feasible_direction) <= 0.
   */
  coco_evaluate_constraint(problem, feasible_direction, &constraint_value);
  
  /* Flip the constraint in "problem" if the chosen feasible_direction
   * is not feasible w.r.t. the constraint in "problem".
   */
  if (constraint_value > 0)
    for (i = 0; i < problem->number_of_variables; ++i)
      data->gradient[i] *= -1.0;
          
  problem->initial_solution = coco_duplicate_vector(feasible_direction, 
      problem->number_of_variables);
 
  data = NULL;  
  return problem;
}

/**
 * @brief Frees the data object.
 */
static void c_linear_gradient_free(void *thing) {
	
  linear_constraint_data_t *data = (linear_constraint_data_t *) thing;
  coco_free_memory(data->gradient);
  coco_free_memory(data->x);
}

/**
 * @brief Allocates a linear constraint problem with all-ones gradient.
 */
static coco_problem_t *c_sum_variables_allocate(const size_t number_of_variables) {

  size_t i;
  coco_problem_t *problem = coco_problem_allocate(number_of_variables, 0, 1);

  problem->evaluate_constraint = c_sum_variables_evaluate;
  
  coco_problem_set_id(problem, "%s_d%02lu", "linearconstraint", number_of_variables);

  for (i = 0; i < number_of_variables; ++i) {
    problem->smallest_values_of_interest[i] = -5.0;
    problem->largest_values_of_interest[i] = 5.0;
  }
  return problem;
}

/**
 * @brief Transforms a linear constraint with all-ones gradient
 *        into a linear constraint whose gradient is passed 
 *        as argument.
 */
static coco_problem_t *c_linear_transform(coco_problem_t *inner_problem, 
                                          const double *gradient) {
  
  linear_constraint_data_t *data;
  coco_problem_t *self;
  data = coco_allocate_memory(sizeof(*data));
  data->gradient = coco_duplicate_vector(gradient, inner_problem->number_of_variables);
  data->x = coco_allocate_vector(inner_problem->number_of_variables);
  self = coco_problem_transformed_allocate(inner_problem, data, 
      c_linear_gradient_free, "gradient_linear_constraint");
  self->evaluate_constraint = c_linear_single_evaluate;

  return self;
}

/**
 * @brief Exchange the first constraint for another one, if any, by
 *        exchanging their gradients.
 */
static coco_problem_t *c_linear_shuffle(coco_problem_t *problem_c,
                                        linear_constraint_data_t *data_c1) {
  
  coco_problem_t *iter_problem;
  coco_problem_stacked_data_t *stacked_data;
  linear_constraint_data_t *constraint_data;
  double aux;
  size_t i, exchanged;
  
  if (problem_c->number_of_constraints < 2)
    return problem_c;
  
  iter_problem = problem_c;
  /* random_number = rand() % (maximum + 1 - minimum) + minimum */
  exchanged = rand() % (problem_c->number_of_constraints + 1 - 2) + 2;
  
  for (i = problem_c->number_of_constraints; i > exchanged; --i) {
    stacked_data = (coco_problem_stacked_data_t*) iter_problem->data;
    iter_problem = stacked_data->problem1;
  }
  
  stacked_data = (coco_problem_stacked_data_t*) iter_problem->data;
  iter_problem = stacked_data->problem2;
  constraint_data = (linear_constraint_data_t *) coco_problem_transformed_get_data(iter_problem);
  
  for (i = 0; i < problem_c->number_of_variables; ++i) {
    aux = constraint_data->gradient[i];
    constraint_data->gradient[i] = data_c1->gradient[i];
    data_c1->gradient[i] = aux;
  }
  
  return problem_c;
}

/**
 * @brief Implements the Marsaglia polar method.
 * 
 * It first generates a pair of independent standard normal random 
 * variables X1 and X2. For having normal random variables with 
 * mean 'mu' and variance 'sigma', it does a simple transform of 
 * the type mu + sigma * X1 and mu + sigma * X2.
 */
double randn(double mu, double sigma) {
	
  double U1, U2, W, mult;
  static double X1, X2;
  static int call = 1;
 
  /* Return the second generated variable from previous call that
   * has not been used yet 
   */
  if (call % 2 == 0) {
    ++call;
    return (mu + sigma * (double) X2);
  }
 
  /* The polar method itself */
  do {
    U1 = -1 + ((double)rand () / RAND_MAX) * 2;
    U2 = -1 + ((double)rand () / RAND_MAX) * 2;
    W = pow(U1, 2) + pow(U2, 2);
  }
  while (W >= 1 || W == 0);
 
  mult = sqrt((-2 * log(W)) / W);
  X1 = U1 * mult;
  X2 = U2 * mult;
 
  ++call;
 
  return (mu + sigma * (double)X1);
}

/**
 * @brief Builds a coco_problem containing one single linear constraint.
 */
static coco_problem_t *c_linear_single_cons_bbob_problem_allocate(const size_t function,
                                                      const size_t dimension,
                                                      const size_t instance,
                                                      const size_t constraint_number,
                                                      const double linear_cons_norm,
                                                      const char *problem_id_template,
                                                      const char *problem_name_template,
                                                      double *gradient,
                                                      const double *feasible_direction) {
																			
  size_t i;
  
  double *gradient_linear_constraint = NULL;
  coco_problem_t *problem = NULL;
  
  long seed_cons;
  
  problem = c_sum_variables_allocate(dimension);
  
  /* Set the gradient of the linear constraint if it is given.
   * This should be the case of the construction of the first 
   * linear constraint only.
   */
  if(gradient) {

    coco_scale_vector(gradient, dimension, linear_cons_norm);
    problem = c_linear_transform(problem, gradient);

  }
  else{ /* Randomly generate the gradient of the linear constraint */
	  
    gradient_linear_constraint = coco_allocate_vector(dimension);
    seed_cons = (long)(function + 10000 * instance 
                                + 50000 * constraint_number);
    srand(seed_cons);
     
    /* Generate a pseudorandom number that is normally distributed
     * with mean mu and variance sigma (= linear_cons_norm)
     */
    for (i = 0; i < dimension; ++i)
      gradient_linear_constraint[i] = randn(0.0, linear_cons_norm);

    problem = c_linear_transform(problem, gradient_linear_constraint);
    coco_free_memory(gradient_linear_constraint);
  }
  
  /* Guarantee that the vector feasible_point is feasible w.r.t. to
   * this constraint and set it as initial_solution
   */
  if(feasible_direction)
    problem = c_guarantee_feasible_point(problem, feasible_direction);
  
  coco_problem_set_id(problem, problem_id_template, function, instance, dimension);
  coco_problem_set_name(problem, problem_name_template, function, instance, dimension);
  coco_problem_set_type(problem, "linear");
  
  return problem;
  
}

/**
 * @brief Builds a coco_problem_t containing all the linear constraints
 *        by stacking them all.
 */
static coco_problem_t *c_linear_cons_bbob_problem_allocate(const size_t function,
                                                      const size_t dimension,
                                                      const size_t instance,
                                                      const size_t number_of_linear_constraints,
                                                      const double linear_cons_norm,
                                                      const char *problem_id_template,
                                                      const char *problem_name_template,
                                                      const double *feasible_direction) {
																																			
  size_t i;
  
  coco_problem_t *problem_c = NULL;
  coco_problem_t *problem_c2 = NULL;
  linear_constraint_data_t *data_c1 = NULL;
  double *gradient_c1 = NULL;
  
  gradient_c1 = coco_allocate_vector(dimension);
  																	
  for (i = 0; i < dimension; ++i)
    gradient_c1[i] = -feasible_direction[i];
  
  /* Build the first linear constraint using 'gradient_c1' as
   * its gradient
   */  
  problem_c = c_linear_single_cons_bbob_problem_allocate(function, 
      dimension, instance, 1, linear_cons_norm, problem_id_template, 
      problem_name_template, gradient_c1, feasible_direction);
  
  /* Store the pointer to the first gradient for later */
  data_c1 = (linear_constraint_data_t *) coco_problem_transformed_get_data(problem_c);
	 
  /* Instantiate the other linear constraints (if any) and stack them 
   * all into problem_c
   */     
  for (i = 2; i <= number_of_linear_constraints; ++i) {
	 
    /* Instantiate a new problem containing one linear constraint only */
    problem_c2 = c_linear_single_cons_bbob_problem_allocate(function, 
        dimension, instance, i, linear_cons_norm, problem_id_template, 
        problem_name_template, NULL, feasible_direction);
		
    problem_c = coco_problem_stacked_allocate(problem_c, problem_c2,
        problem_c2->smallest_values_of_interest, problem_c2->largest_values_of_interest);
	 
    /* Use the standard stacked problem_id as problem_name and 
     * construct a new suite-specific problem_id 
     */
    coco_problem_set_name(problem_c, problem_c->problem_id);
    coco_problem_set_id(problem_c, "bbob-constrained_f%02lu_i%02lu_d%02lu", 
        (unsigned long)function, (unsigned long)instance, (unsigned long)dimension);

    /* Construct problem type */
    coco_problem_set_type(problem_c, "%s_%s", problem_c2->problem_type, 
        problem_c2->problem_type);
  }
  
  /* Exchange the first constraint position for another one if any */
  problem_c = c_linear_shuffle(problem_c, data_c1);
  
  coco_free_memory(gradient_c1);
  
  return problem_c;
 
}

