from setuptools import setup, find_packages

setup(
    name="strutdb-py",
    version="0.1.0",
    description="Python client SDK for StrutDB graph database",
    packages=find_packages(),
    install_requires=[
        "redis>=4.5.0",
    ],
    python_requires=">=3.8",
)
