/*
 * frank_unit_test.c
 *
 * Copyright (c) 2018 Amazon Technologies, Inc.  All rights reserved
 *
 */

/*
 * Instruction for Frank unit test driver
 * Example:
 * cd /sys/kernel/frank_unit_test
 * How to run test:
 * 1. Run all unit test: echo > input or echo all > input
 * 2. Run all test case for X unit test : echo X > input ex. echo thermal > input or echo thermal 0 > input
 * 3. Run #Y test case for X unit test: echo X Y > input ex. echo thermal 2 > input
 * 4. Run #Y test case for X unit test with Z args: echo X Y Z > input ex. echo thermal 2 5000 > input
 *
 * How to see last test input:
 * cat input
 * How to see last test result:
 * cat result
 *
 * If give worng input, you will see error message on consle and test output
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/thermal_framework.h>

#define FRANK_UNIT_TEST_STR_LEN 128

struct frank_unit_test {
	char test_input[FRANK_UNIT_TEST_STR_LEN];
	char test_suite[FRANK_UNIT_TEST_STR_LEN];
	int test_num;
	char test_argvs[FRANK_UNIT_TEST_STR_LEN];
	char test_result[FRANK_UNIT_TEST_STR_LEN];
	struct kobject kobj;
	struct mutex lock;
};

extern int enclosure_thermal_dev_register(struct thermal_dev *tdev);
extern int fconnector_thermal_dev_register(struct thermal_dev *tdev);

static int thermal_test1(struct frank_unit_test *test){
	/*
	do something
	*/
	int ret = 0;
	ret = enclosure_thermal_dev_register(NULL);
	if(ret != -ENODEV){
		snprintf(test->test_result, FRANK_UNIT_TEST_STR_LEN, "Test Fail at %s:enclosure_thermal_dev_register:ret = %d", __FUNCTION__, ret);
		return 1;
	}
	printk("SUCCESS! %s:enclosure_thermal_dev_register:ret = %d\n", __FUNCTION__, ret);
	snprintf(test->test_result, FRANK_UNIT_TEST_STR_LEN, "SUCCESS! %s:enclosure_thermal_dev_register:ret = %d", __FUNCTION__, ret);
	return 0;
}

static int thermal_test2(struct frank_unit_test *test){
	/*
	do something
	*/
	int ret = 0;
	ret = fconnector_thermal_dev_register(NULL);
	if(ret != -ENODEV){
		snprintf(test->test_result, FRANK_UNIT_TEST_STR_LEN, "Test Fail at %s:fconnector_thermal_dev_register:ret = %d", __FUNCTION__, ret);
		return 1;
	}
	printk("SUCCESS! %s:enclosure_thermal_dev_register:ret = %d\n", __FUNCTION__, ret);
	snprintf(test->test_result, FRANK_UNIT_TEST_STR_LEN, "SUCCESS! %s:enclosure_thermal_dev_register:ret = %d", __FUNCTION__, ret);
	return 0;
}

static int thermal_test_all(struct frank_unit_test *test){
	if(thermal_test1(test))
		return 1;
	if(thermal_test2(test))
		return 1;
	return 0;
}

static int frank_unit_test_thermal(struct frank_unit_test *test)
{
	printk("%s\n", __FUNCTION__);
	if(test->test_num == 0)
	{
		if(thermal_test_all(test))
			return 1;
	}
	else if(test->test_num == 1)
	{
		if(thermal_test1(test))
			return 1;
	}
	else if(test->test_num == 2)
	{
		if(thermal_test2(test))
			return 1;
	}
	else
	{
		snprintf(test->test_result, FRANK_UNIT_TEST_STR_LEN, "%s:%s", __FUNCTION__, "Invalid Test Number");
		return 1;
	}
	return 0;
}

static int fan_test1(struct frank_unit_test *test){
	printk("%s\n", __FUNCTION__);
	/*
	do something
	*/
	return 0;
}

static int fan_test2(struct frank_unit_test *test){
	printk("%s\n", __FUNCTION__);
	/*
	do something
	*/
	return 0;
}

static int fan_test_all(struct frank_unit_test *test){
	if(fan_test1(test))
		return 1;
	if(fan_test2(test))
		return 1;
	return 0;
}

static int frank_unit_test_fan(struct frank_unit_test *test)
{
	printk("%s\n", __FUNCTION__);
	if(test->test_num == 0)
	{
		if(fan_test_all(test))
			return 1;
	}
	else if(test->test_num == 1)
	{
		if(fan_test1(test))
			return 1;
	}
	else if(test->test_num == 2)
	{
		if(fan_test2(test))
			return 1;
	}
	else
	{
		snprintf(test->test_result, FRANK_UNIT_TEST_STR_LEN, "%s:%s", __FUNCTION__, "Invalid Test Number");
		return 1;
	}
	return 0;
}

static int uevent_test1(struct frank_unit_test *test){
	char event_string[30];
	char *envp[] = { event_string, NULL };
	struct kobject *kobj = NULL;
	int r;

	printk("%s\n", __FUNCTION__);
	kobj = &(test->kobj);
	if (!kobj) {
	  printk("no kobj\n");
	  return 1;
	}

	snprintf(event_string, 30, "FRANK_REBOOT_UEVENT=%d", 10);
	printk("!!!!Send Uevent!!!! kobj name = %s\n", test->kobj.name);
	r = kobject_uevent_env(kobj, KOBJ_ADD, envp);
	if ( r ) {
	  printk("!!!!Send Uevent failed!!!! r=%d\n", r);
	  return 1;
	}
	else
	  printk("!!!!send Uevent success!!!!\n");

	return 0;
}

static int frank_unit_test_uevent(struct frank_unit_test *test)
{
	printk("%s\n", __FUNCTION__);
	return uevent_test1(test);
}

static int i2c_test1(struct frank_unit_test *test){
	printk("%s\n", __FUNCTION__);
	/*
	do something
	*/
	return 0;
}

static int i2c_test2(struct frank_unit_test *test){
	printk("%s\n", __FUNCTION__);
	/*
	do something
	*/
	return 0;
}

static int i2c_test_all(struct frank_unit_test *test){
	if(i2c_test1(test))
		return 1;
	if(i2c_test2(test))
		return 1;
	return 0;
}

static int frank_unit_test_i2c(struct frank_unit_test *test)
{
	printk("%s\n", __FUNCTION__);
	if(test->test_num == 0)
	{
		if(i2c_test_all(test))
			return 1;
	}
	else if(test->test_num == 1)
	{
		if(i2c_test1(test))
			return 1;
	}
	else if(test->test_num == 2)
	{
		if(i2c_test2(test))
			return 1;
	}
	else
	{
		snprintf(test->test_result, FRANK_UNIT_TEST_STR_LEN, "%s:%s", __FUNCTION__, "Invalid Test Number");
		return 1;
	}
	return 0;
}

static int hdd_test1(struct frank_unit_test *test){
	printk("%s\n", __FUNCTION__);
	/*
	do something
	*/
	return 0;
}

static int hdd_test2(struct frank_unit_test *test){
	printk("%s\n", __FUNCTION__);
	/*
	do something
	*/
	return 0;
}

static int hdd_test_all(struct frank_unit_test *test){
	if(hdd_test1(test))
		return 1;
	if(hdd_test2(test))
		return 1;
	return 0;
}

static int frank_unit_test_hdd(struct frank_unit_test *test)
{
	printk("%s\n", __FUNCTION__);
	if(test->test_num == 0)
	{
		if(hdd_test_all(test))
			return 1;
	}
	else if(test->test_num == 1)
	{
		if(hdd_test1(test))
			return 1;
	}
	else if(test->test_num == 2)
	{
		if(hdd_test2(test))
			return 1;
	}
	else
	{
		snprintf(test->test_result, FRANK_UNIT_TEST_STR_LEN, "%s:%s", __FUNCTION__, "Invalid Test Number");
		return 1;
	}
	return 0;
}

/* run all test suites in default */
/* If any new test suites are added, add them here too */
static int frank_unit_test_all(struct frank_unit_test *test)
{
	printk("%s\n", __FUNCTION__);
	if(frank_unit_test_thermal(test))
		return 1;
	if(frank_unit_test_fan(test))
		return 1;
	if(frank_unit_test_i2c(test))
		return 1;
	if(frank_unit_test_hdd(test))
		return 1;

	return 0;
}

/* run frank unit_test */
/* If any new test suites are added, add them here too */
static int run_frank_unit_test(struct frank_unit_test *test)
{
	char *ts = test->test_suite;
	printk("%s=%s\n", __FUNCTION__, test->test_suite);

	if(strcmp(ts, "all") == 0)
	{
		if(frank_unit_test_all(test))
			return 1;
	}
	else if(strcmp(ts, "thermal") == 0)
	{
		if(frank_unit_test_thermal(test))
			return 1;
	}
	else if(strcmp(ts, "fan") == 0)
	{
		if(frank_unit_test_fan(test))
			return 1;
	}
	else if(strcmp(ts, "i2c") == 0)
	{
		if(frank_unit_test_i2c(test))
			return 1;
	}
	else if(strcmp(ts, "hdd") == 0)
	{
		if(frank_unit_test_hdd(test))
			return 1;
	}
	else if(strcmp(ts, "uevent") == 0)
	{
		if(frank_unit_test_uevent(test))
			return 1;
	}
	else
	{
		snprintf(test->test_result, FRANK_UNIT_TEST_STR_LEN, "%s:%s", __FUNCTION__, "Invalid Test Suite");
		return 1;
	}
	return 0;
}

/* get frank test argument */
static int get_frank_unit_test(struct frank_unit_test *test)
{
	char *test_str = test->test_input;
	char *str;
	int num = 0;
	int len = 0;
	printk("%s:%s", __FUNCTION__, test_str);

	len = strlen(test_str);
	if(test_str[len - 1] == '\n'){
		test_str[len - 1] = '\0';
	}

	/* get test suite */
	str = strsep (&test_str," ");
	if(strcmp(str, "\0") == 0){
		str = "all";
		pr_info("%s:%s\n", __FUNCTION__, "No test suite specified, run all test");
	}
	snprintf(test->test_suite, FRANK_UNIT_TEST_STR_LEN, "%s", str);

	/* get test num */
	str = strsep (&test_str," ");
	if(str == NULL){
		pr_info("%s:%s\n", __FUNCTION__, "No test specified, run all test");
	}else{
		if(kstrtoint(str, 0, &num)){
			snprintf(test->test_result, FRANK_UNIT_TEST_STR_LEN, "%s", "Test number should be number");
			pr_info("%s:%s\n", __FUNCTION__, "Test number should be number");
			return 1;
		}
	}
	test->test_num = num;

	/* get test argvs */
	if(test_str == NULL){
		pr_info("Run unit test:%s:%d\n",test->test_suite, test->test_num);
		snprintf(test->test_input, FRANK_UNIT_TEST_STR_LEN, "%s %d", test->test_suite, test->test_num);
	}
	else{
		snprintf(test->test_argvs, FRANK_UNIT_TEST_STR_LEN, "%s", test_str);
		pr_info("Run unit test:%s:%d:%s\n", test->test_suite, test->test_num, test->test_argvs);
		snprintf(test->test_input, FRANK_UNIT_TEST_STR_LEN, "%s %d %s", test->test_suite, test->test_num, test->test_argvs);
	}

	return 0;
}

/* process frank unit_test */
static int handle_frank_unit_test(struct frank_unit_test *test)
{
	int ret = 0;

	/* reset test result*/
	snprintf(test->test_result, FRANK_UNIT_TEST_STR_LEN, "%s", "No Test Result");

	ret = get_frank_unit_test(test);
	if(ret){
		return ret;
	}

	ret = run_frank_unit_test(test);
	if(ret){
		return ret;
	}

	return ret;
}

static ssize_t unit_test_result_show(struct frank_unit_test *test, char *buf)
{
	if (!test)
		return -EINVAL;
	return snprintf(buf, FRANK_UNIT_TEST_STR_LEN, "%s\n", test->test_result);
}

static ssize_t unit_test_input_show(struct frank_unit_test *test, char *buf)
{
	if (!test)
		return -EINVAL;
	return snprintf(buf, FRANK_UNIT_TEST_STR_LEN, "%s\n", test->test_input);
}

static ssize_t unit_test_input_store(struct frank_unit_test *test, const char *buf, size_t size)
{
	if (!test)
		return -EINVAL;

	snprintf(test->test_input, FRANK_UNIT_TEST_STR_LEN, "%s", buf);
	if (test->test_input == NULL)
		return -EINVAL;
	pr_info("%s:%s", __FUNCTION__, test->test_input);

	/* start unit test */
	mutex_lock(&test->lock);
	if(handle_frank_unit_test(test)){
		pr_info("%s\n", test->test_result);
		mutex_unlock(&test->lock);
		return -EINVAL;
	}
	mutex_unlock(&test->lock);

	pr_info("Test Finish\n");
	return size;
 }

struct unit_test_attribute {
	struct attribute attr;
	ssize_t (*show)(struct frank_unit_test *, char *);
	ssize_t	(*store)(struct frank_unit_test *, const char *, size_t);
};

#define UNIT_TEST_ATTR(_name, _mode, _show, _store) \
	struct unit_test_attribute unit_test_attr_##_name = \
	__ATTR(_name, _mode, _show, _store)

static UNIT_TEST_ATTR(input, S_IRUGO|S_IWUSR, unit_test_input_show, unit_test_input_store);
static UNIT_TEST_ATTR(result, S_IRUGO|S_IWUSR, unit_test_result_show, NULL);

static struct attribute *unit_test_sysfs_attrs[] = {
	&unit_test_attr_input.attr,
	&unit_test_attr_result.attr,
	NULL
};

static ssize_t unit_test_attr_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct frank_unit_test *test;
	struct unit_test_attribute *unit_test_attr;

	test = container_of(kobj, struct frank_unit_test, kobj);
	unit_test_attr = container_of(attr, struct unit_test_attribute, attr);

	if (!test)
		return -EINVAL;

	if (!unit_test_attr->show)
		return -ENOENT;

	return unit_test_attr->show(test, buf);
}

static ssize_t unit_test_attr_store(struct kobject *kobj, struct attribute *attr, const char *buf, size_t size)
{
	struct frank_unit_test *test;
	struct unit_test_attribute *unit_test_attr;

	test = container_of(kobj, struct frank_unit_test, kobj);
	unit_test_attr = container_of(attr, struct unit_test_attribute, attr);

	if (!test)
		return -EINVAL;

	if (!unit_test_attr->store)
		return -ENOENT;

	return unit_test_attr->store(test, buf, size);
}

static const struct sysfs_ops unit_test_sysfs_ops = {
	.show = unit_test_attr_show,
	.store = unit_test_attr_store,
};

static void unit_test_release(struct kobject *kobj)
{
	struct frank_unit_test *test;

	test = container_of(kobj, struct frank_unit_test, kobj);
	kfree(test);
}

static struct kobj_type unit_test_ktype = {
	.sysfs_ops = &unit_test_sysfs_ops,
	.release = unit_test_release,
	.default_attrs = unit_test_sysfs_attrs,
};

static struct frank_unit_test *test;
static struct kset *unit_test_kset;

static int __init frank_unit_test_init(void)
{
	int ret = 0;

	test = kzalloc(sizeof(struct frank_unit_test), GFP_KERNEL);
	if (!test)
		return -ENOMEM;

	unit_test_kset = kset_create_and_add("frank", NULL, kernel_kobj);
	if (!unit_test_kset)
		return -ENOMEM;

	test->kobj.kset = unit_test_kset;
	if (kobject_init_and_add(&test->kobj, &unit_test_ktype, NULL, "unit_test")) {
		kobject_put(&test->kobj);
		pr_err("%s:%s\n", __FUNCTION__, "kobject init and add fail");
		return -EINVAL;
	}
	snprintf(test->test_input, FRANK_UNIT_TEST_STR_LEN, "%s", "No Test Running");
	snprintf(test->test_result, FRANK_UNIT_TEST_STR_LEN, "%s", "No Test Result");
	mutex_init(&test->lock);
	pr_info("frank unit test init\n");
	return ret;
}
module_init(frank_unit_test_init);

static void __exit frank_unit_test_exit(void)
{
	kobject_put(&test->kobj);
	kset_unregister(unit_test_kset);
	pr_info("frank unit_test exit\n");
}
module_exit(frank_unit_test_exit);

MODULE_DESCRIPTION("Frank unit_test Driver");
