#pragma once

struct Compute;
struct Workflow;
struct Description;

int computeCreate(Compute *_compute);
void computeDestroy(Compute *_compute);
int computeCreateWorkflow(Compute *_compute, Workflow *_workflow, const Description *_desc);
int computeExecuteWorkflow();
void computeDestroyWorkflow(Compute *_compute, Workflow *_workflow);

