* T-TEST example pspp code

* Generate an example dataset for male and female humans
* with weight, height, beauty and iq data
* Weight and Height data are generated as normal distributions with
* different mean values. iq is generated with the same mean value (100).
* Beauty is only slightly different.
* Every run of the program will produce new data
input program.

* Females have gender 0
* Create 8 female cases
loop #i = 1 to 8.
 compute weight  = rv.normal (65, 10).
 compute height = rv.normal(170.7,6.3).
 compute beauty = rv.normal (10,4).
 compute iq = rv.normal(100,15).
 compute gender = 0.
 end case.
end loop.

* Males have gender 1
loop #i = 1 to 8.
 compute weight  = rv.normal (83, 13).
 compute height = rv.normal(183.8,7.1).
 compute beauty = rv.normal(11,4).
 compute iq = rv.normal(100,15).
 compute gender = 1.
 end case.
end loop.

end file.
end input program.

* Add a label to the gender values to have descriptive names
value labels
  /gender 0 female 1 male.

* Plot the data as boxplot
examine
  /variables=weight height beauty iq by gender
  /plot=boxplot.

* Do a Scatterplot to check if weight and height
* might be correlated. As both the weight and the 
* height for males is higher than for females
* the combination of male and female data is correlated.
* Weigth increases with height.
graph
  /scatterplot = height with weight.
  
* Within the male and female groups there is no correlation between
* weight and height. This becomes visible by marking male and female
* datapoints with different colour.
graph
  /scatterplot = height with weight by gender.

* The T-Test checks if male and female humans have
* different weight, height, beauty and iq. See that Significance for the
* weight and height variable tends to 0, while the Significance
* for iq should not go to 0. 
* Significance in T-Test means the probablity for the assumption that the
* height (weight, beauty,iq) of the two groups (male,female) have the same
* mean value. As the data for the iq values is generated as normal distribution
* with the same mean value, the significance should not go down to 0.
t-test groups=gender(0,1)
  /variables=weight height beauty iq.  

* Run the Code several times to see the effect that different data
* is generated. Every run is like a new sample from the population.  

* Change the number of samples (cases) by changing the
* loop range to see the effect on significance!
* With increasing number of cases the sample size increases and
* the estimation of mean values and standard deviation becomes better.
* The difference in beauty becomes visible only with larger sample sizes.


